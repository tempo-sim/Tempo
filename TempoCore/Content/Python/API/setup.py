# Copyright Tempo Simulation, LLC. All Rights Reserved

from setuptools import setup
import os

root_dir = os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo")
tempo_module_names = [name for name in os.listdir(root_dir) if os.path.isdir(os.path.join(root_dir, name))]

setup(
    name='tempo',
    version='0.1',
    description='The Tempo simulator Python API',
    packages=["tempo"] + tempo_module_names,
    package_dir={"": "."} | dict([(tempo_module_name, f"./tempo/{tempo_module_name}") for tempo_module_name in tempo_module_names]),
    install_requires=[
        "grpcio==1.62.2",
        "curio==1.6",
        "asyncio==3.4.3"
    ],
    python_requires='>3.9.0'
)
