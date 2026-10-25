# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
from curio.meta import awaitable
import grpc
import os
import sys
import threading


DEFAULT_ADDRESS = "localhost"
DEFAULT_PORT = 10001

# The socket name a server uses when the UnixSocket transport is selected without a path.
DEFAULT_SOCKET_NAME = "tempo.sock"


# The loop synchronous callers run on: one per thread, for the life of the thread.
#
# One loop rather than one per call: a gRPC channel -- and every stream opened on it -- belongs to
# the loop that created it, so a fresh loop per call closed the previous one underneath them. A sync
# `for value in stream_...()` yielded its first value and then stopped, because the second `next()`
# ran on a new loop and found a dead stream, and the channel was rebuilt on every call besides.
#
# Per thread rather than one for the process: a loop runs on one thread at a time, so two threads
# making blocking calls at once on a shared loop had the second fail with "this event loop is
# already running". Each thread's loop gets a channel of its own (TempoContext keeps one per loop).
#
# Never closed, deliberately. A loop lives as long as the thread that is using it, and closing it
# would race whatever still holds a channel on it.
_sync_loops = threading.local()


def _loop_for_sync_calls():
    loop = getattr(_sync_loops, "loop", None)
    if loop is None or loop.is_closed():
        loop = _sync_loops.loop = asyncio.new_event_loop()
    return loop


def run_async(coroutine):
    """
    A helper function to run an async coroutine from a synchronous context.
    """
    try:
        asyncio.get_running_loop()
    except RuntimeError:
        # No running loop, so this is a synchronous caller: run on the one loop kept for them.
        return _loop_for_sync_calls().run_until_complete(coroutine)
    # A loop is running on this very thread, so this is a blocking call made from inside one of its
    # coroutines. Waiting here would stop the loop the reply has to arrive on: a deadlock, every
    # time, rather than a slow call.
    coroutine.close()
    raise RuntimeError(
        "a blocking Tempo call was made from inside a coroutine, on its event loop's own thread, "
        "where it can never finish. Await the call's async form instead."
    )


def default_socket_directory():
    """
    The directory a bare socket name lands in. Mirrors the server's rule (see
    TempoServerEndpoint::GetDefaultSocketDirectory) so that naming a socket "sim-a.sock" means
    the same file on both sides.
    """
    if sys.platform.startswith("linux"):
        runtime_dir = os.environ.get("XDG_RUNTIME_DIR", "")
        if runtime_dir:
            return os.path.join(runtime_dir, "tempo")
    return "/tmp/tempo-{}".format(os.getuid())


def socket_target(path=""):
    """
    The gRPC target for a Unix domain socket. A bare name goes in default_socket_directory();
    anything else is a path. Empty means the default name in that directory, mirroring a server
    configured for the UnixSocket transport without a path.
    """
    if sys.platform == "win32":
        raise RuntimeError(
            "Unix domain sockets are not supported on Windows. Use set_server(address, port)."
        )
    path = path.strip() or DEFAULT_SOCKET_NAME
    if os.sep not in path and (os.altsep is None or os.altsep not in path):
        path = os.path.join(default_socket_directory(), path)
    # gRPC resolves a relative "unix:" path against the client's working directory, which is
    # rarely the server's, so always hand it an absolute one.
    return "unix:" + os.path.abspath(os.path.expanduser(path))


def _target_from_env():
    """
    The endpoint to start from, so one environment variable can point a client and a sim at the
    same endpoint without either of them changing code:

      TEMPO_SERVER_SOCKET   a Unix domain socket, as -ServerSocket= takes
      TEMPO_SERVER_ADDRESS  the server's host, with
      TEMPO_SERVER_PORT     its port, as -ServerPort= takes

    TEMPO_SERVER_SOCKET wins when both are set, matching the server's own precedence. It is ignored
    on Windows, which has no socket transport: this runs at import, so raising here would break
    the whole package rather than just the connection.
    """
    socket_path = os.environ.get("TEMPO_SERVER_SOCKET", "").strip()
    if socket_path and sys.platform != "win32":
        return socket_target(socket_path)
    address = os.environ.get("TEMPO_SERVER_ADDRESS", "").strip() or DEFAULT_ADDRESS
    port = os.environ.get("TEMPO_SERVER_PORT", "").strip() or DEFAULT_PORT
    return "{}:{}".format(address, port)


def set_server(address=DEFAULT_ADDRESS, port=DEFAULT_PORT):
    run_async(tempo_context().set_server(address, port))


@awaitable(set_server)
async def set_server(address=DEFAULT_ADDRESS, port=DEFAULT_PORT):
    await tempo_context().set_server(address, port)


def set_socket(path=""):
    run_async(tempo_context().set_socket(path))


@awaitable(set_socket)
async def set_socket(path=""):
    await tempo_context().set_socket(path)


class _Connection(object):
    """One channel and the stubs made from it. Bound to the loop that opened it, as a channel is."""

    def __init__(self, target):
        self.stubs = {}
        self.channel = grpc.aio.insecure_channel(target,
                                                 options=[
                                                     ('grpc.max_receive_message_length', 1000000000), # 1Gb
                                                 ]
                                                 )


class TempoContext(object):
    def __init__(self):
        self._target = _target_from_env()
        # One connection per event loop. A channel belongs to the loop that created it, and the
        # loops in play are every thread's own sync loop plus whatever loops run coroutines, so one
        # channel rebound to whichever loop asked last would be torn out from under the others.
        self._connections = {}

    async def get_stub(self, stub_class):
        connection = await self._ensure_channel()
        return connection.stubs.setdefault(stub_class, stub_class(connection.channel))

    async def set_server(self, address=DEFAULT_ADDRESS, port=DEFAULT_PORT):
        await self._set_target("{}:{}".format(address, port))

    async def set_socket(self, path=""):
        await self._set_target(socket_target(path))

    @property
    def target(self):
        return self._target

    async def _set_target(self, target):
        if target == self._target:
            return
        self._target = target
        # A channel is bound to the endpoint it was opened on, so pointing the client somewhere
        # else has to drop every one. Otherwise the new endpoint would be ignored for the rest of
        # the process, since _ensure_channel only builds a channel when its loop has none.
        await self._close_channels()

    async def _close_channels(self):
        connections, self._connections = self._connections, {}
        current_loop = asyncio.get_running_loop()
        mine = connections.get(current_loop)
        if mine is not None:
            await mine.channel.close()
        # A channel can only be closed from the loop that created it. The others are released here
        # and reclaimed along with their loops.

    async def _ensure_channel(self):
        current_loop = asyncio.get_running_loop()
        connection = self._connections.get(current_loop)
        if connection is None:
            # A loop that has finished -- asyncio.run's, say -- leaves its connection behind with no
            # way to close it from here; drop those rather than hold one per run ever made.
            for loop in list(self._connections):
                if loop.is_closed():
                    self._connections.pop(loop, None)
            connection = self._connections[current_loop] = _Connection(self._target)
        return connection


# "Singleton Factory" https://stackoverflow.com/questions/12305142/issue-with-singleton-python-call-two-times-init
def tempo_context(_singleton=TempoContext()):
    return _singleton


if __name__ == "__main__":
    sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo_sim"))
