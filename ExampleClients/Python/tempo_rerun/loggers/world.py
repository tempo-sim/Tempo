# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Ground-truth world logger.

Streams actor states near the tracked (ego) actor and logs each as an oriented
3D box plus a transform. All values come straight from ActorState in the
right-handed proto frame.

The oriented box is built from ``local_bounds`` (an axis-aligned box in the
actor's local frame, with scale baked in) placed by the actor's world
``transform`` via the entity's Transform3D — so no per-box rotation is needed.

Actors that leave the query radius (or are destroyed) are cleared, so a drive through a city
doesn't leave a trail of stale boxes behind it.
"""

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
    shown = set()
    last_time_s = [None]
    stopped = [False]   # set once the stream ends, so a handler still running draws nothing more

    def handle(states):
        if visualize and states.actor_states and not stopped[0]:
            last_time_s[0] = states.actor_states[0].timestamp_s
            set_sim_time(conv.SIM_TIME, last_time_s[0])
            now = set()
            for state in states.actor_states:
                if state.name in exclude or _empty(state.local_bounds):
                    continue
                class_id = None
                if classify is not None:
                    class_id = classify(state.name)
                    if class_id is None:
                        continue
                _log_actor_state(state, class_id=class_id)
                now.add(state.name)
            for gone in shown - now:
                rr.log(conv.ground_truth_entity(gone), rr.Clear(recursive=True))
            shown.clear()
            shown.update(now)
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
        if visualize and shown and last_time_s[0] is not None:
            # Rerun's current time is per thread, and the boxes were logged from worker threads, so
            # this thread has to be told when the clears happen.
            set_sim_time(conv.SIM_TIME, last_time_s[0])
            for name in list(shown):
                rr.log(conv.ground_truth_entity(name), rr.Clear(recursive=True))
            shown.clear()


def _empty(box) -> bool:
    """A box with no extent: an actor with nothing visible to bound (e.g. a hidden stand-in)."""
    return (box.max.x - box.min.x) <= 0 and (box.max.y - box.min.y) <= 0 and (box.max.z - box.min.z) <= 0
