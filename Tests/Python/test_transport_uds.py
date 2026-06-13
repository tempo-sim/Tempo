# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration test: confirm the sim's UDS listener answers the same RPC as TCP.

Uses the shared `sim_server` fixture, which leaves the client wired to TCP. Each test below
flips the transport via `set_unix_socket` and restores TCP on teardown so it stays a no-op for
the next test.
"""

import os
import sys

import pytest

import tempo_sim
import tempo_sim.tempo_core as tc

pytestmark = pytest.mark.core

# Same default the sim uses (mirror of UTempoCoreUtils::GetDefaultUnixSocketPath).
SERVER_PORT = int(os.environ.get("TEMPO_SERVER_PORT", "10001"))


@pytest.fixture
def uds_transport(sim_server):
    """Switch the client to the sim's UDS listener for one test, then back to TCP."""
    if sys.platform == "win32":
        # gRPC C++ supports AF_UNIX on Win10+ but the test fleet isn't validated for it yet.
        pytest.skip("UDS integration test skipped on Windows.")
    path = tempo_sim.default_unix_socket_path(SERVER_PORT)
    if not os.path.exists(path):
        pytest.skip(f"Sim is not exposing UDS at {path} (older build?)")
    tempo_sim.set_unix_socket(port=SERVER_PORT)
    try:
        yield path
    finally:
        # Restore TCP so other tests in the session aren't affected.
        tempo_sim.set_server(port=SERVER_PORT)


def test_uds_answers_get_current_level_name(uds_transport):
    resp = tc.get_current_level_name()
    assert resp.level, "expected a non-empty current level name over UDS"


def test_uds_and_tcp_return_equivalent_responses(sim_server):
    """Same RPC twice — once over TCP, once over UDS — should return identical fields.

    The sim time may advance slightly between calls; we only assert structural agreement
    (level name is identical, sim time is finite and non-negative on both).
    """
    if sys.platform == "win32":
        pytest.skip("UDS integration test skipped on Windows.")
    path = tempo_sim.default_unix_socket_path(SERVER_PORT)
    if not os.path.exists(path):
        pytest.skip(f"Sim is not exposing UDS at {path} (older build?)")

    tempo_sim.set_server(port=SERVER_PORT)
    tcp_level = tc.get_current_level_name().level
    tcp_t = tc.get_sim_time().sim_time

    tempo_sim.set_unix_socket(port=SERVER_PORT)
    try:
        uds_level = tc.get_current_level_name().level
        uds_t = tc.get_sim_time().sim_time
    finally:
        tempo_sim.set_server(port=SERVER_PORT)

    assert uds_level == tcp_level, (
        f"level name should match across transports: tcp={tcp_level!r} uds={uds_level!r}"
    )
    assert uds_t >= 0.0 and tcp_t >= 0.0
