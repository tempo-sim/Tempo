# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
import grpc
import os
import sys


def get_event_loop():
    try:
        loop = asyncio.get_running_loop()
    except RuntimeError:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
    return loop


def set_server(address="localhost", port=10001):
    loop = get_event_loop()
    loop.run_until_complete(tempo_context()._set_server(address, port))


class TempoContext(object):
    def __init__(self):
        self.server_address = "localhost"
        self.server_port = "10001"
        self._init_channel()
        self.stubs = {}

    def get_stub(self, stub_class):
        return self.stubs.setdefault(stub_class, stub_class(self.channel))

    async def _set_server(self, address="localhost", port="10001"):
        self.server_address = address
        self.server_port = port
        self._init_channel()

    def _init_channel(self):
        self.channel = grpc.aio.insecure_channel("{}:{}".format(self.server_address, self.server_port),
                                                 options=[
                                                     ('grpc.max_receive_message_length', 1000000000), # 1Gb
                                                 ]
                                                 )
        self.stubs = {}


# "Singleton Factory" https://stackoverflow.com/questions/12305142/issue-with-singleton-python-call-two-times-init
def tempo_context(_singleton=TempoContext()):
    return _singleton


if __name__ == "__main__":
    sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo"))
