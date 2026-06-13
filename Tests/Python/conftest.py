# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Shared pytest fixtures for the Tempo Python API integration tests.

These tests run the generated `tempo_sim` client against a PACKAGED Tempo build (not the
editor). The `sim_server` session fixture launches the packaged binary headless, waits until
its gRPC server answers, and tears it down at the end of the session. Tests that need a
running sim depend on `sim_server`; the `contract` tests do not, so a contract-only run never
launches the binary.

Configuration comes from the environment (set by Scripts/TestPythonAPI.sh):
  TEMPO_PACKAGED_BINARY        Path to the packaged launcher (e.g. Packaged/Linux/<Project>.sh).
  TEMPO_SERVER_PORT            gRPC port the sim should listen on (default 10001).
  TEMPO_SIM_STARTUP_TIMEOUT_S  How long to wait for the server to come up (default 300).
  TEMPO_SIM_RENDER             "1" to render off-screen (sensors group); else -nullrhi.
  TEMPO_TEST_REPORT_DIR        If set, the sim's full log is written here (sim.log) so CI uploads it.
"""

import os
import subprocess
import tempfile
import time

import pytest

SERVER_PORT = int(os.environ.get("TEMPO_SERVER_PORT", "10001"))
PACKAGED_BINARY = os.environ.get("TEMPO_PACKAGED_BINARY", "")
STARTUP_TIMEOUT_S = float(os.environ.get("TEMPO_SIM_STARTUP_TIMEOUT_S", "300"))


def _tail(path, n=200):
    try:
        with open(path, "r", errors="replace") as f:
            return "".join(f.readlines()[-n:])
    except OSError:
        return "(no log captured)"


@pytest.fixture(scope="session")
def sim_server():
    if not PACKAGED_BINARY:
        pytest.fail(
            "TEMPO_PACKAGED_BINARY is not set. Run these tests via Scripts/TestPython.sh, or "
            "point it at the packaged launcher (e.g. Packaged/Linux/<Project>.sh)."
        )
    if not os.path.exists(PACKAGED_BINARY):
        pytest.fail(f"Packaged binary not found at {PACKAGED_BINARY}")

    import tempo_sim
    import tempo_sim.tempo_core as tc

    # RHI mode: the core/world/movement groups don't render, so they run with -nullrhi (no GPU
    # needed). The sensors group sets TEMPO_SIM_RENDER=1 to render off-screen (and wants a GPU).
    render = os.environ.get("TEMPO_SIM_RENDER", "0") == "1"
    rhi_args = ["-RenderOffScreen"] if render else ["-nullrhi"]
    args = [
        PACKAGED_BINARY,
        *rhi_args,
        "-unattended",
        "-nopause",
        "-nosound",
        "-nosplash",
        f"-ServerPort={SERVER_PORT}",
        "-stdout",
        "-fullstdoutlogoutput",
    ]
    # Capture the sim's full stdout/stderr. Prefer the report dir (TEMPO_TEST_REPORT_DIR) so the log
    # is uploaded as a CI artifact for post-mortem; fall back to a temp file locally.
    report_dir = os.environ.get("TEMPO_TEST_REPORT_DIR")
    if report_dir:
        os.makedirs(report_dir, exist_ok=True)
        log = open(os.path.join(report_dir, "sim.log"), "wb")
    else:
        log = tempfile.NamedTemporaryFile(prefix="tempo-sim-", suffix=".log", delete=False)
    print(f"Sim log: {log.name}")
    proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

    tempo_sim.set_server(port=SERVER_PORT)
    deadline = time.monotonic() + STARTUP_TIMEOUT_S
    last_err = None
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            log.flush()
            pytest.fail(
                f"Sim exited early (exit code {proc.returncode}). Log tail:\n{_tail(log.name)}"
            )
        try:
            tc.get_current_level_name()
            break
        except Exception as e:  # gRPC server not accepting connections yet
            last_err = e
            time.sleep(1.0)
    else:
        proc.terminate()
        pytest.fail(
            f"Sim gRPC server did not become ready within {STARTUP_TIMEOUT_S}s "
            f"(last error: {last_err}). Log tail:\n{_tail(log.name)}"
        )

    yield proc

    # Graceful shutdown first, then escalate.
    try:
        tc.quit()
    except Exception:
        pass
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()


@pytest.fixture
def fixed_step(sim_server):
    """Put the sim in deterministic fixed-step mode (10 steps/s), paused with sim time
    grid-aligned, so a test can advance time by an exact number of full steps. Returns
    the steps-per-second."""
    import tempo_sim.tempo_core as tc
    import tempo_sim.TempoCore.Time_pb2 as Time

    steps_per_second = 10
    tc.set_time_mode(time_mode=Time.TM_FIXED_STEP)
    tc.set_sim_steps_per_second(sim_steps_per_second=steps_per_second)
    tc.pause()
    # Entering fixed-step mode from an arbitrary wall-clock sim time leaves sim time
    # off the fixed-step grid. The first fixed-step frame only advances by the partial
    # amount needed to reach the next grid line (snap-to-grid), not a full 1/SPS step.
    # Whether that priming frame ticks before `pause()` takes effect is a race (it
    # usually does locally, but not under CI load), so do it explicitly here. After
    # this step sim time is grid-aligned and every subsequent step advances exactly
    # 1/SPS, making the per-step assertions deterministic.
    tc.step()
    return steps_per_second
