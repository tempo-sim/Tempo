# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
from curio.meta import awaitable
import grpc
import os
import sys


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


def default_unix_socket_path(port=10001):
    """Return the default UDS path the Tempo server listens on for the given port.

    Mirrors UTempoCoreUtils::GetDefaultUnixSocketPath so the client picks up the same
    socket the server creates without any explicit configuration.
    """
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")
        if not base:
            base = os.path.expandvars("%USERPROFILE%\\AppData\\Local")
        return os.path.join(base, "Temp", f"tempo-{port}.sock")
    return f"/tmp/tempo-{port}.sock"


def set_server(address="localhost", port=10001):
    run_async(tempo_context().set_server(address, port))


@awaitable(set_server)
async def set_server(address="localhost", port=10001):
    await tempo_context().set_server(address, port)


def set_unix_socket(path=None, port=10001):
    run_async(tempo_context().set_unix_socket(path, port))


@awaitable(set_unix_socket)
async def set_unix_socket(path=None, port=10001):
    await tempo_context().set_unix_socket(path, port)


class TempoContext(object):
    def __init__(self):
        self._server_address = "localhost"
        self._server_port = 10001
        # Default to UDS so a freshly-imported client connects to a same-host sim with no setup.
        # Calling set_server() flips this to "tcp".
        self._mode = "uds"
        self._socket_path = default_unix_socket_path(self._server_port)
        self._stubs = {}
        self._channel = None
        self._channel_loop = None

    async def get_stub(self, stub_class):
        await self._ensure_channel()
        return self._stubs.setdefault(stub_class, stub_class(self._channel))

    async def set_server(self, address="localhost", port=10001):
        self._mode = "tcp"
        self._server_address = address
        self._server_port = port
        await self._reset_channel()

    async def set_unix_socket(self, path=None, port=10001):
        self._mode = "uds"
        self._server_port = port
        self._socket_path = path if path is not None else default_unix_socket_path(port)
        await self._reset_channel()

    async def _reset_channel(self):
        if self._channel is not None:
            await self._channel.close()
        self._channel = None
        self._channel_loop = None
        self._stubs = {}

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
        options = [
            ('grpc.max_receive_message_length', 1000000000),  # 1Gb
        ]
        if self._mode == "uds":
            target = "unix:{}".format(self._socket_path)
        else:
            target = "{}:{}".format(self._server_address, self._server_port)
        self._channel = grpc.aio.insecure_channel(target, options=options)


# "Singleton Factory" https://stackoverflow.com/questions/12305142/issue-with-singleton-python-call-two-times-init
def tempo_context(_singleton=TempoContext()):
    return _singleton


if __name__ == "__main__":
    sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo_sim"))
