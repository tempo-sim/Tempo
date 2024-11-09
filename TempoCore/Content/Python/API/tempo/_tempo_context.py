# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
import grpc
import os
import sys


def run_async(coroutine):
    """
    A helpful utility to run an async command from any
    (synchronous or asynchronous) context.
    """
    try:
        # This will raise RuntimeError if we are in a synchronous context
        asyncio.get_running_loop()
        # If it didn't raise we are in an asynchronous context and can just return the coroutine
        return coroutine
    except RuntimeError:
        # This is a synchronous context, but there still might be a loop running elsewhere in the process
        # There does not seem to be a way of detecting this situation without accessing the protected members below
        running_loop = asyncio.get_event_loop_policy()._local._loop
        if running_loop is not None:
            # Submit the coroutine to that loop, if it exists
            running_loop.run_until_complete(coroutine)
        else:
            # Otherwise, use asyncio.run to create a new loop and close it after running
            asyncio.run(coroutine)


def set_server(address="localhost", port=10001):
    run_async(tempo_context()._set_server(address, port))


class TempoContext(object):
    def __init__(self):
        self.server_address = "localhost"
        self.server_port = "10001"
        self.stubs = {}
        self.channel = None
        self.channel_loop = None
        run_async(self._init_channel())

    async def get_stub(self, stub_class):
        if self.channel is None:
            await self._init_channel()
        elif self.channel_loop is not asyncio.get_running_loop():
            # We are in a different loop than the one our channel is running on
            # Reinitialize our channel on the current one
            await self._init_channel()
        return self.stubs.setdefault(stub_class, stub_class(self.channel))

    async def _set_server(self, address="localhost", port="10001"):
        self.server_address = address
        self.server_port = port
        await self._init_channel()

    async def _init_channel(self):
        self.stubs = {}
        self.channel_loop = asyncio.get_running_loop()
        self.channel = grpc.aio.insecure_channel("{}:{}".format(self.server_address, self.server_port),
                                                 options=[
                                                     ('grpc.max_receive_message_length', 1000000000), # 1Gb
                                                 ]
                                                 )


# "Singleton Factory" https://stackoverflow.com/questions/12305142/issue-with-singleton-python-call-two-times-init
def tempo_context(_singleton=TempoContext()):
    return _singleton


if __name__ == "__main__":
    sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo"))
