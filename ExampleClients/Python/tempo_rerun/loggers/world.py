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

import threading

import numpy as np
import rerun as rr

import tempo_sim.tempo_world as tw

from .. import conventions as conv
from .._compat import set_sim_time
from ..streaming import pump


def _log_actor_state(state, entity=None, class_id=None, show_labels=True):
    entity = entity or conv.ground_truth_entity(state.name)
    rr.log(entity, conv.transform_to_rerun(state.transform))
    _log_actor_box(state, entity, class_id, show_labels)


def _log_actor_box(state, entity, class_id=None, show_labels=True):
    center, half = conv.box_center_half(state.local_bounds)
    # The box goes on a child, which inherits the actor's transform. Rerun drops a box's centers on
    # the entity a 3D view takes as its origin, so a box logged on the followed actor itself (the
    # ego) would be drawn centered on the actor's pivot instead: half a car height too low.
    # A class id colors the box by the AnnotationContext, as the label images are.
    rr.log(f"{entity}/box", rr.Boxes3D(centers=[center], half_sizes=[half], labels=[state.name],
                                       class_ids=[class_id] if class_id is not None else None,
                                       show_labels=show_labels))


async def stream_ego(track_actor, *, entity=None, visualize=True, taps=(), show_labels=True, sink=None):
    """Stream the tracked actor itself (the near-query may not include its center).
    `entity` overrides where it is logged (default: its ground-truth entity). `show_labels` False
    draws its box without the actor's name. `sink`, if given, receives each ActorState to show
    instead of this logging it (tempo_rerun.ego_frame)."""
    taps = tuple(taps)

    def handle(state):
        if visualize:
            if sink is not None:
                sink(state)
            else:
                set_sim_time(conv.SIM_TIME, state.timestamp_s)
                _log_actor_state(state, entity, show_labels=show_labels)
        for tap in taps:
            tap(state)

    await pump(tw.stream_actor_state(actor=track_actor), handle, label=f"ego:{track_actor}")


async def stream_ground_truth(track_actor, *, search_radius_m=200.0, include_static=False,
                              exclude=(), classify=None, visualize=True, taps=(), show_labels=True,
                              sink=None):
    """Stream the actors near `track_actor`. `exclude` names actors not to draw (the ego, which
    stream_ego draws). `classify`, if given, maps an actor name to a class id to color its box by,
    or None to not draw it. `show_labels` False draws the boxes without the actors' names. Taps
    receive the whole ActorStates message, unfiltered. `sink`, if given, receives each frame's
    ``(time_s, states, class_ids)`` -- the states to draw, and their class ids or None -- to show
    instead of this logging them (tempo_rerun.ego_frame)."""
    taps = tuple(taps)
    exclude = set(exclude)
    shown = [False]     # whether the last set logged had any boxes
    last_time_s = [None]
    stopped = [False]   # set once the stream ends, so a handler still running draws nothing more
    # The stop check and the log happen together under this lock, so the final Clear can't land
    # before a handler's late log.
    log_lock = threading.Lock()

    def handle(states):
        if visualize and states.actor_states and not stopped[0]:
            time_s = states.actor_states[0].timestamp_s
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
            ids = class_ids if classify is not None else None
            if sink is not None:
                sink(time_s, kept, ids)
            else:
                # One log call for every box, not two per actor: each call waits its turn for the
                # interpreter lock against the camera handlers, and 60 of them took half a second
                # a frame, so most frames' boxes were dropped. Each frame's set also replaces the
                # last whole, so an actor that left needs no Clear.
                with log_lock:
                    if not stopped[0]:
                        last_time_s[0] = time_s
                        set_sim_time(conv.SIM_TIME, time_s)
                        rr.log(GROUND_TRUTH_ENTITY, _boxes(kept, ids, show_labels=show_labels))
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
        with log_lock:
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


def _boxes(states, class_ids=None, show_labels=True, frame=None) -> rr.Boxes3D:
    """The actors' oriented boxes in the world frame: the local box's center placed by the actor's
    transform, and the actor's rotation, as _log_actor_state draws one through a Transform3D.
    `frame`, a TempoCore.Transform, expresses them in that frame instead."""
    n = len(states)
    centers = np.empty((n, 3), np.float32)
    halves = np.empty((n, 3), np.float32)
    quats = np.empty((n, 4), np.float32)
    if frame is not None:
        f_loc, f_rot = frame.location, frame.rotation
        f_q = conv.euler_rpy_to_quat(f_rot.r, f_rot.p, f_rot.y)
        f_inv = [-f_q[0], -f_q[1], -f_q[2], f_q[3]]
        f_origin = np.array([f_loc.x, f_loc.y, f_loc.z])
    for i, state in enumerate(states):
        center, half = conv.box_center_half(state.local_bounds)
        loc, rot = state.transform.location, state.transform.rotation
        q = conv.euler_rpy_to_quat(rot.r, rot.p, rot.y)
        c = np.add([loc.x, loc.y, loc.z], _rotate(q, center))
        if frame is not None:
            c = _rotate(f_inv, c - f_origin)
            q = _quat_mul(f_inv, q)
        centers[i] = c
        halves[i] = half
        quats[i] = q
    return rr.Boxes3D(centers=centers, half_sizes=halves, quaternions=quats,
                      labels=[s.name for s in states], class_ids=class_ids, show_labels=show_labels)


def _quat_mul(a, b):
    """Hamilton product a * b of quaternions (x, y, z, w): rotate by b, then by a."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def _rotate(q, v):
    """Vector v rotated by unit quaternion q (x, y, z, w)."""
    x, y, z, w = q
    u = np.array([x, y, z])
    v = np.asarray(v, dtype=np.float64)
    return v + 2.0 * np.cross(u, np.cross(u, v) + w * v)


def _empty(box) -> bool:
    """A box with no extent: an actor with nothing visible to bound (e.g. a hidden stand-in)."""
    return (box.max.x - box.min.x) <= 0 and (box.max.y - box.min.y) <= 0 and (box.max.z - box.min.z) <= 0
