# Copyright Tempo Simulation, LLC. All Rights Reserved

"""The request timeout: a deadline on unary calls, overridable for a stretch of them, and never on a
stream. Against an in-process server, so it needs only the wheel."""

import asyncio
import time
from concurrent import futures

import grpc
import pytest

import tempo_sim
import tempo_sim.tempo_core as tc
import tempo_sim.tempo_world as tw
from tempo_sim._tempo_context import run_async, tempo_context
from tempo_sim.TempoCore import Time_pb2, Time_pb2_grpc
from tempo_sim.TempoWorld import WorldState_pb2, WorldState_pb2_grpc

pytestmark = pytest.mark.contract

# Comfortably longer than the deadlines below, and short enough to keep the suite fast.
SLOW_S = 0.5
DEADLINE_S = 0.1


class _SlowTime(Time_pb2_grpc.TimeServiceServicer):
    def GetSimTime(self, request, context):
        time.sleep(SLOW_S)
        return Time_pb2.GetSimTimeResponse()


class _QuietWorldState(WorldState_pb2_grpc.WorldStateServiceServicer):
    def StreamActorState(self, request, context):
        time.sleep(SLOW_S)
        yield WorldState_pb2.ActorState()


@pytest.fixture
def slow_server():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    Time_pb2_grpc.add_TimeServiceServicer_to_server(_SlowTime(), server)
    WorldState_pb2_grpc.add_WorldStateServiceServicer_to_server(_QuietWorldState(), server)
    port = server.add_insecure_port("127.0.0.1:0")
    server.start()
    previous_target = tempo_context().target
    tempo_sim.set_server("127.0.0.1", port)
    try:
        yield
    finally:
        tempo_sim.set_request_timeout(None)
        run_async(tempo_context()._set_target(previous_target))
        server.stop(0)


def test_no_deadline_by_default(slow_server):
    assert tempo_sim.request_timeout() is None
    tc.get_sim_time()


def test_a_unary_call_past_its_deadline_fails(slow_server):
    tempo_sim.set_request_timeout(DEADLINE_S)
    start = time.monotonic()
    with pytest.raises(grpc.RpcError) as error:
        tc.get_sim_time()
    assert error.value.code() == grpc.StatusCode.DEADLINE_EXCEEDED
    assert time.monotonic() - start < SLOW_S


def test_an_override_wins_for_its_block(slow_server):
    tempo_sim.set_request_timeout(DEADLINE_S)
    with tempo_sim.with_request_timeout(None):
        assert tempo_sim.request_timeout() is None
        tc.get_sim_time()
    assert tempo_sim.request_timeout() == DEADLINE_S


def test_an_override_reaches_async_calls(slow_server):
    async def calls():
        with tempo_sim.with_request_timeout(DEADLINE_S):
            await tc.get_sim_time()

    with pytest.raises(grpc.RpcError) as error:
        asyncio.run(calls())
    assert error.value.code() == grpc.StatusCode.DEADLINE_EXCEEDED


def test_a_stream_has_no_deadline(slow_server):
    tempo_sim.set_request_timeout(DEADLINE_S)
    states = list(tw.stream_actor_state(actor="anything"))
    assert len(states) == 1
