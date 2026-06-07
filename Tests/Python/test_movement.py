# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for the TempoMovement command API against a packaged sim.

These depend on the packaged map containing at least one commandable pawn. If it doesn't, the
motion test skips (rather than fails) so the group stays green on content-light maps.
"""

import pytest

import tempo_sim.tempo_core as tc
import tempo_sim.tempo_movement as tm
import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

pytestmark = pytest.mark.movement


def test_get_commandable_pawns_listing(sim_server):
    resp = tm.get_commandable_pawns()
    # May be empty depending on the map; just assert the response shape and that names are strings.
    for pawn in resp.pawns:
        assert isinstance(pawn, str) and pawn


def test_command_velocity_moves_a_pawn(fixed_step):
    pawns = list(tm.get_commandable_pawns().pawns)
    if not pawns:
        pytest.skip("No commandable pawns in the packaged map")

    steps_per_second = fixed_step
    pawn = pawns[0]

    start = tw.get_current_actor_state(actor=pawn)

    # Body-frame forward velocity (m/s, right-handed). Magnitude of displacement is checked, so
    # the pawn's heading doesn't matter.
    twist = Geometry.Twist()
    twist.linear.x = 2.0
    tm.command_velocity(pawn=pawn, twist=twist)

    # ~3 seconds of sim time — comfortably past the controller's acceleration ramp.
    tc.advance_steps(steps=3 * steps_per_second)

    end = tw.get_current_actor_state(actor=pawn)
    dx = end.transform.location.x - start.transform.location.x
    dy = end.transform.location.y - start.transform.location.y
    distance = (dx * dx + dy * dy) ** 0.5

    assert distance > 0.5, f"commanded pawn did not move (moved {distance:.3f} m)"
