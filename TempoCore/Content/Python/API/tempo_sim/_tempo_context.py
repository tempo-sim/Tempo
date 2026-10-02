# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
from curio.meta import awaitable
import grpc
import os
import sys


DEFAULT_ADDRESS = "localhost"
DEFAULT_PORT = 10001

# The socket name a server uses when the UnixSocket transport is selected without a path.
DEFAULT_SOCKET_NAME = "tempo.sock"


def run_async(coroutine):
    """
    A helper function to run an async coroutine from a synchronous context.
    """
    try:
        # If there is a running loop run the coroutine on it
        running_loop = asyncio.get_running_loop()
        future = asyncio.run_coroutine_threadsafe(coroutine, running_loop)
        return future.result()
    except RuntimeError:
        # No running loop - create a new one and run the coroutine on it
        return asyncio.run(coroutine)


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

    TEMPO_SERVER_SOCKET wins when both are set, matching the server's own precedence.
    """
    socket_path = os.environ.get("TEMPO_SERVER_SOCKET", "").strip()
    if socket_path:
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


class TempoContext(object):
    def __init__(self):
        self._target = _target_from_env()
        self._stubs = {}
        self._channel = None
        self._channel_loop = None

    async def get_stub(self, stub_class):
        await self._ensure_channel()
        return self._stubs.setdefault(stub_class, stub_class(self._channel))

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
        # else has to drop it. Otherwise the new endpoint would be ignored for the rest of the
        # process, since _ensure_channel only builds a channel when there isn't one.
        await self._close_channel()

    async def _close_channel(self):
        channel, channel_loop = self._channel, self._channel_loop
        self._channel = None
        self._channel_loop = None
        self._stubs = {}
        if channel is None:
            return
        try:
            current_loop = asyncio.get_running_loop()
        except RuntimeError:
            current_loop = None
        if channel_loop is current_loop:
            await channel.close()
        # A channel can only be closed from the loop that created it. One left over from another
        # loop is released here and reclaimed along with that loop.

    async def _ensure_channel(self):
        current_loop = asyncio.get_running_loop()
        if self._channel is None or self._channel_loop != current_loop:
            # Close existing channel if there is one
            if self._channel is not None:
                await self._channel.close()
            # Create new channel on current loop
            self._init_channel()
            self._channel_loop = current_loop

    def _init_channel(self):
        self._stubs = {}
        self._channel = grpc.aio.insecure_channel(self._target,
                                                 options=[
                                                     ('grpc.max_receive_message_length', 1000000000), # 1Gb
                                                 ]
                                                 )


# "Singleton Factory" https://stackoverflow.com/questions/12305142/issue-with-singleton-python-call-two-times-init
def tempo_context(_singleton=TempoContext()):
    return _singleton


if __name__ == "__main__":
    sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo_sim"))
