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
        "set_actor_transform", "set_float_property", "set_property", "batch", "call",
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


def test_call_builder_stages_typed_args():
    # The Call builder is what keeps the Value wrapper out of user code: one *_arg method per
    # Value variant, with the struct-shaped ones flattened the way set_vector_property is.
    import tempo_sim.tempo_world as tw

    call = (tw.call(actor="MyActor", function="ApplyDamage")
            .float_arg("Amount", 42.5)
            .vector_arg("Location", x=1.0, y=2.0, z=3.0)
            .actor_arg("Instigator", "BP_Player_C_0")
            .string_array_arg("Tags", ["alpha", "bravo"]))

    names = [arg.name for arg in call._args]
    assert names == ["Amount", "Location", "Instigator", "Tags"]

    values = [arg.value for arg in call._args]
    assert values[0].float_value == pytest.approx(42.5)
    assert (values[1].vector_value.x, values[1].vector_value.y, values[1].vector_value.z) == (1.0, 2.0, 3.0)
    assert values[2].actor_value == "BP_Player_C_0"
    assert list(values[3].string_array_value.values) == ["alpha", "bravo"]


def test_call_builder_covers_every_value_variant():
    # Every Value oneof variant must get an *_arg method, or a type is reachable through the
    # singular set_*_property RPCs but not through CallFunction.
    import tempo_sim.tempo_world as tw
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    variants = WorldControl.Value.DESCRIPTOR.oneofs_by_name["value"].fields
    expected = {f.name[:-len("_value")] + "_arg" for f in variants}
    missing = [m for m in sorted(expected) if not callable(getattr(tw.Call, m, None))]
    assert not missing, f"Call builder is missing: {missing}"
