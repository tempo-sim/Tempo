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


def set_server(address="localhost", port=10001):
    run_async(tempo_context().set_server(address, port))


@awaitable(set_server)
async def set_server(address="localhost", port=10001):
    await tempo_context().set_server(address, port)


class TempoContext(object):
    def __init__(self):
        self._server_address = "localhost"
        self._server_port = 10001
        self._stubs = {}
        self._channel = None
        self._channel_loop = None

    async def get_stub(self, stub_class):
        await self._ensure_channel()
        return self._stubs.setdefault(stub_class, stub_class(self._channel))

    async def set_server(self, address="localhost", port=10001):
        self._server_address = address
        self._server_port = port

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
        self._channel = grpc.aio.insecure_channel("{}:{}".format(self._server_address, self._server_port),
                                                 options=[
                                                     ('grpc.max_receive_message_length', 1000000000), # 1Gb
                                                 ]
                                                 )


# "Singleton Factory" https://stackoverflow.com/questions/12305142/issue-with-singleton-python-call-two-times-init
def tempo_context(_singleton=TempoContext()):
    return _singleton


if __name__ == "__main__":
    sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo"))
