# Copyright Tempo Simulation, LLC. All Rights Reserved

"""A gRPC completion for an event loop that has closed must not stop the client.

grpc.aio hands every call's completion to its call's loop from one process-wide poller thread. Where
a loop cannot watch a file descriptor (the Proactor loop that is Windows' default), that thread calls
the loop's call_soon_threadsafe itself and nothing catches what it raises, so one completion for a
closed loop -- a stream an asyncio.run left open, cancelled once its channel is collected -- ends the
thread, and every call in the process then waits forever on a server that has answered it.
tempo_sim's _tolerate_closing has each loop it connects on drop such a completion instead.

The scenario runs in a child process: what it provokes is process-wide and permanent. It forces the
no-file-descriptor path with a loop whose add_reader is unsupported, as a Proactor loop's is, so it
runs the same on every platform. Against an in-process server, so it needs only the wheel.

Also here: closing a generated stream ends its RPC, which is what keeps a stream that is closed early
from being cancelled only once the garbage collector reaches it, with the loop long gone."""

import subprocess
import sys
import textwrap

import pytest

pytestmark = pytest.mark.contract

ITERATIONS = 20
HANG_EXIT_CODE = 3

SCENARIO = textwrap.dedent(f"""
    import asyncio
    import os
    import sys
    import threading
    import time
    from concurrent import futures

    import grpc

    import tempo_sim
    import tempo_sim._tempo_context as tempo_context
    import tempo_sim.tempo_core as tc
    import tempo_sim.tempo_world as tw
    from tempo_sim.TempoCore import Time_pb2, Time_pb2_grpc
    from tempo_sim.TempoWorld import WorldState_pb2, WorldState_pb2_grpc

    if "--without-fix" in sys.argv:
        tempo_context._tolerate_closing = lambda loop: None


    class NoFdMonitoringLoop(asyncio.SelectorEventLoop):
        # What grpc.aio meets in a Proactor loop: no add_reader, so the poller thread dispatches.
        def add_reader(self, *args, **kwargs):
            raise NotImplementedError


    class Policy(asyncio.DefaultEventLoopPolicy):
        def new_event_loop(self):
            return NoFdMonitoringLoop()


    asyncio.set_event_loop_policy(Policy())


    class Time(Time_pb2_grpc.TimeServiceServicer):
        def GetSimTime(self, request, context):
            return Time_pb2.GetSimTimeResponse()


    class WorldState(WorldState_pb2_grpc.WorldStateServiceServicer):
        def StreamActorState(self, request, context):
            while context.is_active():
                yield WorldState_pb2.ActorState()
                time.sleep(0.01)


    # Enough workers for every stream left open below, which each hold one until it is cancelled.
    server = grpc.server(futures.ThreadPoolExecutor(max_workers={ITERATIONS} + 8))
    Time_pb2_grpc.add_TimeServiceServicer_to_server(Time(), server)
    WorldState_pb2_grpc.add_WorldStateServiceServicer_to_server(WorldState(), server)
    port = server.add_insecure_port("127.0.0.1:0")
    server.start()
    tempo_sim.set_server("127.0.0.1", port)


    # Once the poller has stopped, anything that waits on a call hangs: the blocking calls, and
    # asyncio.run's own shutdown cancelling what is left. So one watchdog over the whole scenario.
    progress = [time.monotonic()]


    def watchdog():
        while time.monotonic() - progress[0] < 10:
            time.sleep(0.2)
        sys.stderr.write("no progress for 10 s\\n")
        sys.stderr.flush()
        os._exit({HANG_EXIT_CODE})


    threading.Thread(target=watchdog, daemon=True).start()


    def blocking_call():
        tc.get_sim_time()
        progress[0] = time.monotonic()


    async def leave_a_stream_open():
        # A raw call: a generated stream cancels its call when it is closed, and asyncio.run closes
        # it on the way out. A call held anywhere else -- a stub used directly, say -- outlives it.
        stub = await tempo_context.tempo_context().get_stub(WorldState_pb2_grpc.WorldStateServiceStub)
        call = stub.StreamActorState(WorldState_pb2.ActorStateRequest(actor="anything"))
        await call.read()
        return call  # outlives the loop, as a stream the caller never closed does


    blocking_call()
    for _ in range({ITERATIONS}):
        left_open = asyncio.run(leave_a_stream_open())
        del left_open
        time.sleep(0.05)
        blocking_call()
    server.stop(0)
    print("ok")
""")


def _run_scenario(*args):
    return subprocess.run([sys.executable, "-c", SCENARIO, *args], capture_output=True, text=True,
                          timeout=10 * (ITERATIONS + 2))


# A generated stream must end its RPC when it is closed, without waiting for the garbage collector:
# until then the sim goes on writing to a stream no one reads, and in fixed step waits on that write.
CLOSE_SCENARIO = textwrap.dedent('''
    import asyncio
    import gc
    import os
    import sys
    import threading
    import time
    from concurrent import futures

    import grpc

    import tempo_sim
    import tempo_sim.tempo_world as tw
    from tempo_sim.TempoWorld import WorldState_pb2, WorldState_pb2_grpc

    ended = threading.Event()


    class WorldState(WorldState_pb2_grpc.WorldStateServiceServicer):
        def StreamActorState(self, request, context):
            context.add_callback(ended.set)
            while context.is_active():
                yield WorldState_pb2.ActorState()
                time.sleep(0.01)


    server = grpc.server(futures.ThreadPoolExecutor(max_workers=8))
    WorldState_pb2_grpc.add_WorldStateServiceServicer_to_server(WorldState(), server)
    port = server.add_insecure_port("127.0.0.1:0")
    server.start()
    tempo_sim.set_server("127.0.0.1", port)
    gc.disable()  # what is being checked is that nothing waits for it


    def check(how):
        if not ended.wait(5.0):
            # The open stream holds a server thread, which would keep the process from exiting.
            sys.stderr.write(f"closing {how} stream left its RPC open\\n")
            sys.stderr.flush()
            os._exit(1)
        ended.clear()


    async def read_one_then_close():
        stream = tw.stream_actor_state(actor="anything").__aiter__()
        await stream.__anext__()
        await stream.aclose()
        check("an async")

    asyncio.run(read_one_then_close())


    async def leave_early():
        async for _ in tw.stream_actor_state(actor="anything"):
            break
        # async for doesn't close what it leaves; asyncio.run's shutdown does, after this returns.

    asyncio.run(leave_early())
    check("an abandoned async")

    stream = tw.stream_actor_state(actor="anything")
    next(stream)
    stream.close()
    check("a blocking")

    server.stop(0)
    print("ok")
''')


def test_closing_a_stream_ends_its_rpc():
    result = subprocess.run([sys.executable, "-c", CLOSE_SCENARIO], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, f"{result.stdout}\n{result.stderr}"


def test_a_completion_for_a_closed_loop_does_not_stop_the_client():
    result = _run_scenario()
    assert result.returncode != HANG_EXIT_CODE, (
        "a blocking call hung after an asyncio.run left a stream open: grpc.aio's poller thread "
        f"stopped. stderr:\n{result.stderr}")
    assert result.returncode == 0, f"scenario failed:\n{result.stdout}\n{result.stderr}"


def test_the_scenario_reaches_the_grpc_aio_failure():
    """Without the fix the scenario must hang, or the test above proves nothing."""
    result = _run_scenario("--without-fix")
    if result.returncode == 0:
        pytest.skip("this grpcio no longer stops its poller thread on a completion for a closed loop; "
                    "tempo_sim._tempo_context._tolerate_closing may no longer be needed")
    assert result.returncode == HANG_EXIT_CODE, f"scenario failed otherwise:\n{result.stdout}\n{result.stderr}"
    assert "Event loop is closed" in result.stderr
