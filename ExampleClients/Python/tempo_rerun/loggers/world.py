# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Ground-truth world logger.

Streams actor states near the tracked (ego) actor and logs them, each frame, as one batch of
oriented 3D boxes in the world frame. The ego itself is logged as a box under a Transform3D
(stream_ego), since the 3D view follows it. All values come straight from ActorState in the
right-handed proto frame.

Each oriented box is built from ``local_bounds`` (an axis-aligned box in the actor's local frame,
with scale baked in) placed by the actor's world ``transform``.

Each frame's batch replaces the last, so actors that leave the query radius (or are destroyed)
disappear with it, and a drive through a city doesn't leave a trail of stale boxes behind it.
"""

import numpy as np
import rerun as rr

import tempo_sim.tempo_world as tw

from .. import conventions as conv
from .._compat import set_sim_time
from ..streaming import pump


def _log_actor_state(state, entity=None, class_id=None):
    entity = entity or conv.ground_truth_entity(state.name)
    center, half = conv.box_center_half(state.local_bounds)
    rr.log(entity, conv.transform_to_rerun(state.transform))
    # The box goes on a child, which inherits the actor's transform. Rerun drops a box's centers on
    # the entity a 3D view takes as its origin, so a box logged on the followed actor itself (the
    # ego) would be drawn centered on the actor's pivot instead: half a car height too low.
    # A class id colors the box by the AnnotationContext, as the label images are.
    rr.log(f"{entity}/box", rr.Boxes3D(centers=[center], half_sizes=[half], labels=[state.name],
                                       class_ids=[class_id] if class_id is not None else None))


async def stream_ego(track_actor, *, entity=None, visualize=True, taps=()):
    """Stream the tracked actor itself (the near-query may not include its center).
    `entity` overrides where it is logged (default: its ground-truth entity)."""
    taps = tuple(taps)

    def handle(state):
        if visualize:
            set_sim_time(conv.SIM_TIME, state.timestamp_s)
            _log_actor_state(state, entity)
        for tap in taps:
            tap(state)

    await pump(tw.stream_actor_state(actor=track_actor), handle, label=f"ego:{track_actor}")


async def stream_ground_truth(track_actor, *, search_radius_m=200.0, include_static=False,
                              exclude=(), classify=None, visualize=True, taps=()):
    """Stream the actors near `track_actor`. `exclude` names actors not to draw (the ego, which
    stream_ego draws). `classify`, if given, maps an actor name to a class id to color its box by,
    or None to not draw it. Taps receive the whole ActorStates message, unfiltered."""
    taps = tuple(taps)
    exclude = set(exclude)
    shown = [False]     # whether the last set logged had any boxes
    last_time_s = [None]
    stopped = [False]   # set once the stream ends, so a handler still running draws nothing more

    def handle(states):
        if visualize and states.actor_states and not stopped[0]:
            last_time_s[0] = states.actor_states[0].timestamp_s
            set_sim_time(conv.SIM_TIME, last_time_s[0])
            kept, class_ids = [], []
            for state in states.actor_states:
                if state.name in exclude or _empty(state.local_bounds):
                    continue
                if classify is not None:
                    class_id = classify(state.name)
                    if class_id is None:
                        continue
                    class_ids.append(class_id)
                kept.append(state)
            # One log call for every box, not two per actor: each call waits its turn for the
            # interpreter lock against the camera handlers, and 60 of them took half a second a
            # frame, so most frames' boxes were dropped. Each frame's set also replaces the last
            # whole, so an actor that left needs no Clear.
            rr.log(GROUND_TRUTH_ENTITY, _boxes(kept, class_ids if classify is not None else None))
            shown[0] = bool(kept)
        for tap in taps:
            tap(states)

    try:
        await pump(
            tw.stream_actor_states_near(
                near_actor=track_actor,
                search_radius_m=search_radius_m,
                include_static=include_static,
            ),
            handle, label="ground_truth",
        )
    finally:
        stopped[0] = True
        # Take down every box this stream drew. Otherwise, when a client stops it to follow another
        # actor, the last boxes stay in the viewer for good, frozen where they were; one of them can be
        # the next actor followed, which then carries a stale box on top of its own.
        if visualize and shown[0] and last_time_s[0] is not None:
            # Rerun's current time is per thread, and the boxes were logged from worker threads, so
            # this thread has to be told when the clear happens.
            set_sim_time(conv.SIM_TIME, last_time_s[0])
            rr.log(GROUND_TRUTH_ENTITY, rr.Clear(recursive=True))
            shown[0] = False


# Every actor's box near the ego, as one batch.
GROUND_TRUTH_ENTITY = f"{conv.WORLD}/ground_truth"


def _boxes(states, class_ids=None) -> rr.Boxes3D:
    """The actors' oriented boxes in the world frame: the local box's center placed by the actor's
    transform, and the actor's rotation, as _log_actor_state draws one through a Transform3D."""
    n = len(states)
    centers = np.empty((n, 3), np.float32)
    halves = np.empty((n, 3), np.float32)
    quats = np.empty((n, 4), np.float32)
    for i, state in enumerate(states):
        center, half = conv.box_center_half(state.local_bounds)
        loc, rot = state.transform.location, state.transform.rotation
        q = conv.euler_rpy_to_quat(rot.r, rot.p, rot.y)
        centers[i] = np.add([loc.x, loc.y, loc.z], _rotate(q, center))
        halves[i] = half
        quats[i] = q
    return rr.Boxes3D(centers=centers, half_sizes=halves, quaternions=quats,
                      labels=[s.name for s in states], class_ids=class_ids)


def _rotate(q, v):
    """Vector v rotated by unit quaternion q (x, y, z, w)."""
    x, y, z, w = q
    u = np.array([x, y, z])
    v = np.asarray(v, dtype=np.float64)
    return v + 2.0 * np.cross(u, np.cross(u, v) + w * v)


def _empty(box) -> bool:
    """A box with no extent: an actor with nothing visible to bound (e.g. a hidden stand-in)."""
    return (box.max.x - box.min.x) <= 0 and (box.max.y - box.min.y) <= 0 and (box.max.z - box.min.z) <= 0
