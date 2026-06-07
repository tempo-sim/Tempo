# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Contract tests for the generated `tempo_sim` package.

These need only the installed wheel — no running sim — so they double as a fast smoke test
that the codegen pipeline produced an importable package with the expected per-module API
surface. They run as the `contract` fan-out group.
"""

import importlib
import inspect

import pytest

pytestmark = pytest.mark.contract

# A representative slice of the flattened, per-module API. Not exhaustive — just enough that a
# broken codegen run (missing module, renamed RPC, dropped wrapper) fails loudly and early.
EXPECTED_SURFACE = {
    "tempo_sim.tempo_core": [
        "set_time_mode", "set_sim_steps_per_second", "advance_steps",
        "play", "pause", "step", "get_sim_time", "get_current_level_name",
    ],
    "tempo_sim.tempo_world": [
        "spawn_actor", "destroy_actor", "get_all_actors", "get_current_actor_state",
        "set_actor_transform", "set_float_property",
    ],
    "tempo_sim.tempo_movement": [
        "get_commandable_pawns", "command_vehicle", "command_velocity", "command_acceleration",
    ],
    "tempo_sim.tempo_sensors": [
        "get_available_sensors", "stream_color_images", "stream_lidar_scans",
    ],
}


def test_package_root_imports():
    import tempo_sim

    assert callable(tempo_sim.set_server)
    assert callable(tempo_sim.run_async)


@pytest.mark.parametrize("module_name,funcs", sorted(EXPECTED_SURFACE.items()))
def test_module_exposes_expected_functions(module_name, funcs):
    module = importlib.import_module(module_name)
    missing = [f for f in funcs if not callable(getattr(module, f, None))]
    assert not missing, f"{module_name} is missing: {missing}"


def test_generated_signatures_have_named_params():
    # The generated wrappers take keyword params named after the proto fields (e.g. set_time_mode
    # takes time_mode). Spot-check one so a codegen change that drops the named params is caught.
    import tempo_sim.tempo_core as tc

    assert "time_mode" in inspect.signature(tc.set_time_mode).parameters

    import tempo_sim.tempo_world as tw

    spawn_params = inspect.signature(tw.spawn_actor).parameters
    assert "actor_type" in spawn_params
    assert "transform" in spawn_params


def test_proto_message_modules_importable():
    # The pb2 modules must ship alongside the wrappers and expose the message/enum types the
    # wrappers reference.
    import tempo_sim.TempoCore.Time_pb2 as Time
    import tempo_sim.TempoCore.Geometry_pb2 as Geometry

    assert Time.TM_FIXED_STEP != Time.TM_WALL_CLOCK
    # Construct a couple of messages to confirm the descriptors are well-formed.
    Geometry.Transform()
    Geometry.Twist()
