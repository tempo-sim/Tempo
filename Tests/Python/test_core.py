# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for the TempoCore time/control API against a packaged sim."""

import pytest

import tempo_sim.tempo_core as tc
import tempo_sim.TempoCore.Time_pb2 as Time

pytestmark = pytest.mark.core


def test_current_level_name_is_reported(sim_server):
    resp = tc.get_current_level_name()
    assert resp.level, "expected a non-empty current level name"


def test_get_sim_time_returns_a_value(sim_server):
    resp = tc.get_sim_time()
    # sim_time is a double in seconds; it should be finite and non-negative.
    assert resp.sim_time >= 0.0


def test_fixed_step_advances_time_deterministically(fixed_step):
    # `fixed_step` leaves the sim paused in fixed-step mode at `fixed_step` steps/second.
    steps_per_second = fixed_step
    expected_dt = 1.0 / steps_per_second

    t0 = tc.get_sim_time().sim_time
    tc.step()
    t1 = tc.get_sim_time().sim_time

    assert t1 - t0 == pytest.approx(expected_dt, abs=1e-6), (
        f"one step at {steps_per_second}/s should advance sim time by {expected_dt}s, "
        f"got {t1 - t0}s"
    )


def test_advance_steps_advances_by_n_steps(fixed_step):
    steps_per_second = fixed_step
    n = 5
    t0 = tc.get_sim_time().sim_time
    tc.advance_steps(steps=n)
    t1 = tc.get_sim_time().sim_time

    assert t1 - t0 == pytest.approx(n / steps_per_second, abs=1e-6)


def test_paused_sim_does_not_advance(fixed_step):
    # In fixed-step mode while paused (and without stepping), sim time should hold.
    t0 = tc.get_sim_time().sim_time
    t1 = tc.get_sim_time().sim_time
    assert t1 == pytest.approx(t0, abs=1e-9)
