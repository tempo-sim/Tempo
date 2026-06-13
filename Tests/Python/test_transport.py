# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Contract tests for the client-side transport state machine.

These exercise `TempoContext`'s mode-switching and URI generation without ever opening a
real gRPC channel — `_init_channel` is patched so we capture the target string it would
hand to `grpc.aio.insecure_channel`. No sim required.
"""

import os
from unittest.mock import patch

import pytest

import tempo_sim
from tempo_sim._tempo_context import TempoContext, default_unix_socket_path

pytestmark = pytest.mark.contract


def _captured_target(ctx: TempoContext) -> str:
    """Drive _init_channel and return the gRPC target string it would have used."""
    captured = {}

    def fake_channel(target, options=None):
        captured["target"] = target
        # Return a sentinel that won't be touched (we don't await anything on it).
        return object()

    with patch("tempo_sim._tempo_context.grpc.aio.insecure_channel", side_effect=fake_channel):
        ctx._init_channel()
    return captured["target"]


def test_default_socket_path_is_port_keyed():
    p10001 = default_unix_socket_path(10001)
    p10002 = default_unix_socket_path(10002)
    assert p10001 != p10002, "different ports must produce different default paths"
    assert "10001" in p10001 and "10002" in p10002
    if os.name == "nt":
        assert p10001.endswith(".sock")
        assert "Temp" in p10001
    else:
        assert p10001 == "/tmp/tempo-10001.sock"


def test_default_mode_is_uds():
    ctx = TempoContext()
    target = _captured_target(ctx)
    assert target.startswith("unix:"), f"expected unix: URI by default, got {target}"
    assert target == "unix:" + default_unix_socket_path(10001)


@pytest.mark.asyncio_skip  # marker for callers; we run set_server via run_async below
def test_set_server_switches_to_tcp():
    ctx = TempoContext()
    tempo_sim.run_async(ctx.set_server("192.0.2.5", 10005))
    target = _captured_target(ctx)
    assert target == "192.0.2.5:10005"


def test_set_unix_socket_with_explicit_path():
    ctx = TempoContext()
    tempo_sim.run_async(ctx.set_unix_socket("/var/run/tempo-test.sock"))
    target = _captured_target(ctx)
    assert target == "unix:/var/run/tempo-test.sock"


def test_set_unix_socket_with_port_derives_default_path():
    ctx = TempoContext()
    tempo_sim.run_async(ctx.set_unix_socket(None, port=10042))
    target = _captured_target(ctx)
    assert target == "unix:" + default_unix_socket_path(10042)


def test_round_trip_tcp_then_uds():
    ctx = TempoContext()
    tempo_sim.run_async(ctx.set_server("127.0.0.1", 10001))
    assert _captured_target(ctx) == "127.0.0.1:10001"
    tempo_sim.run_async(ctx.set_unix_socket("/tmp/back-to-uds.sock"))
    assert _captured_target(ctx) == "unix:/tmp/back-to-uds.sock"
