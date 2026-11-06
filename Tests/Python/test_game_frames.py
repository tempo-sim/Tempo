# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for FixedStep game frames (StepsPerGameFrame) against a packaged sim."""

import time

import pytest

import tempo_sim.tempo_core as tc
import tempo_sim.tempo_world as tw

pytestmark = pytest.mark.core

STEPS_PER_GAME_FRAME = 4


@pytest.fixture
def game_frames(fixed_step):
    """Group fixed steps into game frames of STEPS_PER_GAME_FRAME, starting paused on a game frame
    boundary. Returns the step length in seconds."""
    tc.set_steps_per_game_frame(steps_per_game_frame=STEPS_PER_GAME_FRAME)
    yield 1.0 / fixed_step
    # Held until the game frame boundary if a test left a game frame part-way through, which also
    # finishes it.
    tc.set_steps_per_game_frame(steps_per_game_frame=1)


def sim_time():
    return tc.get_sim_time().sim_time


def test_one_step_per_game_frame_ticks_a_game_frame_every_step(fixed_step):
    response = tc.step()
    assert response.game_frame
    assert response.steps_until_game_frame == 1


def test_a_client_that_keeps_stepping_gets_one_frame_per_step(fixed_step):
    # While a client keeps stepping, the world waits between frames for its next Step and runs it in
    # the very next frame, rather than spending a paused frame receiving it and another running it.
    # Capping the frame rate makes the frame count visible in wall-clock time.
    max_fps = 20
    frame_seconds = 1.0 / max_fps
    original = tc.get_console_variable(name="t.MaxFPS").value
    tc.set_console_variable(name="t.MaxFPS", value=str(max_fps))
    try:
        tc.step()
        steps = 10
        start = time.monotonic()
        for _ in range(steps):
            tc.step()
        elapsed = time.monotonic() - start
    finally:
        tc.set_console_variable(name="t.MaxFPS", value=original)
    assert elapsed < 1.5 * steps * frame_seconds, (
        f"{steps} steps took {elapsed:.2f}s at {max_fps} fps: about {elapsed / frame_seconds / steps:.1f} "
        "frames per step"
    )


def test_each_step_advances_sim_time_by_one_step(game_frames):
    step_seconds = game_frames
    t0 = sim_time()
    for i in range(1, 2 * STEPS_PER_GAME_FRAME + 1):
        tc.step()
        assert sim_time() == pytest.approx(t0 + i * step_seconds, abs=1e-9), f"after step {i}"


def test_step_reports_where_it_left_the_game_frame(game_frames):
    for i in range(1, 2 * STEPS_PER_GAME_FRAME + 1):
        response = tc.step()
        into = i % STEPS_PER_GAME_FRAME
        assert response.game_frame == (into == 0), f"after step {i}"
        assert response.steps_until_game_frame == STEPS_PER_GAME_FRAME - into, f"after step {i}"


def test_advance_steps_counts_steps_across_game_frames(game_frames):
    step_seconds = game_frames
    t0 = sim_time()
    tc.step()
    # From one step in: the rest of this game frame, a whole one, and two steps of the next.
    steps = (STEPS_PER_GAME_FRAME - 1) + STEPS_PER_GAME_FRAME + 2
    response = tc.advance_steps(steps=steps)
    assert sim_time() == pytest.approx(t0 + (1 + steps) * step_seconds, abs=1e-9)
    assert not response.game_frame
    assert response.steps_until_game_frame == STEPS_PER_GAME_FRAME - 2


def test_advance_steps_that_end_a_game_frame_report_it(game_frames):
    response = tc.advance_steps(steps=2 * STEPS_PER_GAME_FRAME)
    assert response.game_frame
    assert response.steps_until_game_frame == STEPS_PER_GAME_FRAME


def test_timestamps_between_game_frames_are_the_steps(game_frames):
    # The reason world time moves between frames: a handler that timestamps with world time reports
    # the step's time, not the last frame's.
    step_seconds = game_frames
    actors = tw.get_all_actors().actors
    if not actors:
        pytest.skip("the map has no actors to query")
    t0 = sim_time()
    for i in range(1, STEPS_PER_GAME_FRAME):
        tc.step()
        state = tw.get_current_actor_state(actor=actors[0].name)
        assert state.timestamp_s == pytest.approx(t0 + i * step_seconds, abs=1e-9), f"after step {i}"


def test_game_frame_only_rpc_part_way_through_finishes_the_game_frame(game_frames):
    step_seconds = game_frames
    t0 = sim_time()
    tc.step()
    # A time settings RPC must wait for a game frame: the game frame runs the rest of its steps first.
    tc.set_sim_steps_per_second(sim_steps_per_second=int(round(1.0 / step_seconds)))
    assert sim_time() == pytest.approx(t0 + STEPS_PER_GAME_FRAME * step_seconds, abs=1e-9)
    # Game frames carry on from the boundary, on the same grid.
    response = tc.step()
    assert sim_time() == pytest.approx(t0 + (STEPS_PER_GAME_FRAME + 1) * step_seconds, abs=1e-9)
    assert response.steps_until_game_frame == STEPS_PER_GAME_FRAME - 1


def test_console_command_waits_for_a_game_frame_but_engine_info_does_not(game_frames):
    step_seconds = game_frames
    t0 = sim_time()
    tc.step()
    tc.get_engine_info()
    max_fps = tc.get_console_variable(name="t.MaxFPS").value
    assert sim_time() == pytest.approx(t0 + step_seconds, abs=1e-9)
    # A console variable typed as a command is the one command every build handles the same way.
    tc.exec_console_command(command=f"t.MaxFPS {max_fps}")
    assert sim_time() == pytest.approx(t0 + STEPS_PER_GAME_FRAME * step_seconds, abs=1e-9)


def test_changing_steps_per_game_frame_counts_from_the_change(game_frames):
    tc.step()
    # Held until the game frame ends, then the next game frame is a whole one of the new size.
    tc.set_steps_per_game_frame(steps_per_game_frame=3)
    for i in range(1, 4):
        response = tc.step()
        assert response.steps_until_game_frame == 3 - i % 3, f"after step {i}"
    assert response.game_frame


def test_pause_part_way_through_holds_and_keeps_the_grid(game_frames):
    step_seconds = game_frames
    t0 = sim_time()
    tc.step()
    tc.pause()
    assert sim_time() == pytest.approx(t0 + step_seconds, abs=1e-9)
    # Paused frames run while the client is quiet, and the game frame picks up where it left off.
    tc.get_current_level_name()
    assert sim_time() == pytest.approx(t0 + step_seconds, abs=1e-9)
    response = tc.advance_steps(steps=STEPS_PER_GAME_FRAME - 1)
    assert sim_time() == pytest.approx(t0 + STEPS_PER_GAME_FRAME * step_seconds, abs=1e-9)
    assert response.game_frame


def test_play_right_after_a_game_frame_only_rpc_part_way_through_keeps_playing(game_frames):
    # The held RPC runs the game frame to its end with an internal step that re-pauses when its frame
    # ends. A Play that lands in that same frame must end the step rather than be undone by its
    # re-pause.
    step_seconds = game_frames
    tc.step()
    tc.set_sim_steps_per_second(sim_steps_per_second=int(round(1.0 / step_seconds)))
    tc.play()
    t0 = sim_time()
    time.sleep(0.5)
    assert sim_time() > t0, "the sim was paused again after play()"
    tc.pause()


def test_sim_time_never_goes_back_across_game_frames(game_frames):
    previous = sim_time()
    for _ in range(3 * STEPS_PER_GAME_FRAME):
        tc.step()
        now = sim_time()
        assert now > previous
        previous = now
