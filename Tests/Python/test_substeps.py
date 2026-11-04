# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for FixedStep substeps (SubstepsPerStep, the Substep RPC) against a packaged sim."""

import pytest

import tempo_sim.tempo_core as tc
import tempo_sim.tempo_world as tw

pytestmark = pytest.mark.core

SUBSTEPS_PER_STEP = 4


@pytest.fixture
def substeps(fixed_step):
    """Divide each fixed step into SUBSTEPS_PER_STEP substeps, starting paused on a step boundary.
    Returns (step_seconds, substep_seconds)."""
    tc.set_substeps_per_step(substeps_per_step=SUBSTEPS_PER_STEP)
    step_seconds = 1.0 / fixed_step
    yield step_seconds, step_seconds / SUBSTEPS_PER_STEP
    # Held until the step boundary if a test left a step part-way through, which also finishes it.
    tc.set_substeps_per_step(substeps_per_step=1)


def sim_time():
    return tc.get_sim_time().sim_time


def test_each_substep_advances_sim_time_by_one_substep(substeps):
    step_seconds, substep_seconds = substeps
    t0 = sim_time()
    for i in range(1, 2 * SUBSTEPS_PER_STEP + 1):
        tc.substep()
        assert sim_time() == pytest.approx(t0 + i * substep_seconds, abs=1e-9), f"after substep {i}"
    assert sim_time() == pytest.approx(t0 + 2 * step_seconds, abs=1e-9)


def test_timestamps_between_substeps_are_the_substeps(substeps):
    # The reason world time moves between frames: a handler that timestamps with world time reports
    # the substep's time, not the last frame's.
    _, substep_seconds = substeps
    actors = tw.get_all_actors().actors
    if not actors:
        pytest.skip("the map has no actors to query")
    t0 = sim_time()
    for i in range(1, SUBSTEPS_PER_STEP):
        tc.substep()
        state = tw.get_current_actor_state(actor=actors[0].name)
        assert state.timestamp_s == pytest.approx(t0 + i * substep_seconds, abs=1e-9), f"after substep {i}"


def test_step_part_way_through_finishes_the_step(substeps):
    step_seconds, _ = substeps
    t0 = sim_time()
    tc.substep()
    tc.substep()
    tc.step()
    assert sim_time() == pytest.approx(t0 + step_seconds, abs=1e-9)
    # And the next step is a whole one.
    tc.step()
    assert sim_time() == pytest.approx(t0 + 2 * step_seconds, abs=1e-9)


def test_advance_steps_part_way_through_counts_the_rest_as_one(substeps):
    step_seconds, _ = substeps
    t0 = sim_time()
    tc.substep()
    tc.advance_steps(steps=3)
    assert sim_time() == pytest.approx(t0 + 3 * step_seconds, abs=1e-9)


def test_step_boundary_rpc_part_way_through_finishes_the_step(substeps):
    step_seconds, substep_seconds = substeps
    t0 = sim_time()
    tc.substep()
    # A time settings RPC must wait for the step boundary: the step runs to its end first.
    tc.set_sim_steps_per_second(sim_steps_per_second=int(round(1.0 / step_seconds)))
    assert sim_time() == pytest.approx(t0 + step_seconds, abs=1e-9)
    # Substeps carry on from the boundary, on the same grid.
    tc.substep()
    assert sim_time() == pytest.approx(t0 + step_seconds + substep_seconds, abs=1e-9)


def test_console_command_waits_for_the_step_boundary_but_engine_info_does_not(substeps):
    step_seconds, substep_seconds = substeps
    t0 = sim_time()
    tc.substep()
    tc.get_engine_info()
    max_fps = tc.get_console_variable(name="t.MaxFPS").value
    assert sim_time() == pytest.approx(t0 + substep_seconds, abs=1e-9)
    # A console variable typed as a command is the one command every build handles the same way.
    tc.exec_console_command(command=f"t.MaxFPS {max_fps}")
    assert sim_time() == pytest.approx(t0 + step_seconds, abs=1e-9)


def test_pause_part_way_through_holds_and_keeps_the_grid(substeps):
    step_seconds, substep_seconds = substeps
    t0 = sim_time()
    tc.substep()
    tc.pause()
    assert sim_time() == pytest.approx(t0 + substep_seconds, abs=1e-9)
    # Paused frames run while the client is quiet, and the step picks up where it left off.
    tc.get_current_level_name()
    assert sim_time() == pytest.approx(t0 + substep_seconds, abs=1e-9)
    for _ in range(SUBSTEPS_PER_STEP - 1):
        tc.substep()
    assert sim_time() == pytest.approx(t0 + step_seconds, abs=1e-9)


def test_sim_time_never_goes_back_across_frames(substeps):
    previous = sim_time()
    for _ in range(3 * SUBSTEPS_PER_STEP):
        tc.substep()
        now = sim_time()
        assert now > previous
        previous = now
