# Copyright Tempo Simulation, LLC. All Rights Reserved

from setuptools import setup
import os

script_dir = os.path.dirname(os.path.realpath(__file__))
root_dir = os.path.join(script_dir, "tempo")
tempo_module_names = [name for name in os.listdir(root_dir) if os.path.isdir(os.path.join(root_dir, name))]

with open(os.path.join(script_dir, "requirements.txt")) as f:
    install_requires = [line.strip() for line in f if line.strip() and not line.startswith("#")]

setup(
    name='tempo',
    version='0.1',
    description='The Tempo simulator Python API',
    packages=["tempo"] + tempo_module_names,
    package_dir={"": "."} | dict([(tempo_module_name, f"./tempo/{tempo_module_name}") for tempo_module_name in tempo_module_names]),
    install_requires=install_requires,
    python_requires='>3.9.0'
)
