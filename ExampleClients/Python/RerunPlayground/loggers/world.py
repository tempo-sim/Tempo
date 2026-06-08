# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Ground-truth world logger.

Streams actor states near the tracked (ego) actor and logs each as an oriented
3D box plus a transform. All values come straight from ActorState in the
right-handed proto frame.

The oriented box is built from ``local_bounds`` (an axis-aligned box in the
actor's local frame, with scale baked in) placed by the actor's world
``transform`` via the entity's Transform3D — so no per-box rotation is needed.
"""

import rerun as rr

import tempo_sim.tempo_world as tw

from .. import conventions as conv
from .._compat import set_sim_time
from ..streaming import pump


def _log_actor_state(state):
    entity = conv.ground_truth_entity(state.name)
    center, half = conv.box_center_half(state.local_bounds)
    rr.log(entity, conv.transform_to_rerun(state.transform))
    rr.log(entity, rr.Boxes3D(centers=[center], half_sizes=[half], labels=[state.name]))


async def stream_ego(track_actor):
    """Stream the tracked actor itself (the near-query may not include its center)."""
    def handle(state):
        set_sim_time(conv.SIM_TIME, state.timestamp_s)
        _log_actor_state(state)

    await pump(tw.stream_actor_state(actor=track_actor), handle, label=f"ego:{track_actor}")


async def stream_ground_truth(cfg, track_actor):
    def handle(states):
        if not states.actor_states:
            return
        set_sim_time(conv.SIM_TIME, states.actor_states[0].timestamp_s)
        for state in states.actor_states:
            _log_actor_state(state)

    await pump(
        tw.stream_actor_states_near(
            near_actor=track_actor,
            search_radius_m=cfg.search_radius_m,
            include_static=cfg.include_static,
        ),
        handle, label="ground_truth",
    )
