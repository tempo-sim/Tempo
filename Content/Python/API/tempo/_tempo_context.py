# Copyright Tempo Simulation, LLC. All Rights Reserved

import grpc
import os
import sys


class TempoContext(object):
    _instance = None

    def __new__(cls):
        if not cls._instance:
            cls._instance = super(TempoContext, cls).__new__(cls)
        return cls._instance

    def __init__(self):
        self.set_server_address("localhost:10002")
        self.stubs = {}
        self.message_classes = {}

    def get_stub(self, stub_class):
        return self.stubs.setdefault(stub_class, stub_class(self.channel))

    def set_server_address(self, server_address):
        self.channel = grpc.aio.insecure_channel(server_address,
            options=[
                ('grpc.max_receive_message_length', 1000000000), # 1Gb
            ]
        )


if __name__ == "__main__":
    sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo"))
