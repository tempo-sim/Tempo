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
        "curio-compat==1.6.7",
        "asyncio==3.4.3",
        "protobuf==4.25.3",
        "opencv-python==4.10.0.84",
        "matplotlib==3.9.2",
        "pynput==1.7.7",
        "pyvista==0.46.1",
        "pyvistaqt==0.11.3",
        "qasync==0.27.1",
        "PyQt5==5.15.11"
    ],
    python_requires='>3.9.0'
)
