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
        "get_actor_properties", "get_actor_functions", "get_component_functions",
        # scale3d, not scale3_d: pascal_to_snake keeps a capital that follows a digit in the
        # same word, so the digit-bearing RPC names stay readable.
        "set_actor_location", "set_actor_rotation", "set_actor_scale3d",
        "get_actor_transform", "get_actor_location", "get_actor_rotation", "get_actor_scale3d",
        "get_component_transform", "get_component_location", "get_component_rotation",
        "get_component_scale3d",
        "set_vector2d_property",
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
    assert callable(tempo_sim.set_socket)
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



MIGRATION = "https://tempo-sim.readthedocs.io/en/latest/migration/v0.4.0/"


def _wrapper_for(module, rpc_name):
    # pascal_to_snake is the generator's, not the wheel's; a wrapper's name less its underscores
    # is the RPC's, which is all a lookup needs.
    return next(getattr(module, name) for name in dir(module)
                if not name.startswith("_") and name.replace("_", "") == rpc_name.lower())


def test_every_deprecated_rpc_is_marked_deprecated():
    # Read off the descriptors, so an RPC deprecated later is covered without being named here.
    import tempo_sim.tempo_world as tw
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    service = WorldControl.DESCRIPTOR.services_by_name["WorldControlService"]
    deprecated = [m.name for m in service.methods if m.GetOptions().deprecated]
    assert "SetBoolProperty" in deprecated and "SetProperties" in deprecated
    for rpc_name in deprecated:
        assert "is deprecated; see " + MIGRATION in (_wrapper_for(tw, rpc_name).__doc__ or ""), rpc_name
    for rpc_name in ("SetProperty", "GetProperty", "RunBatch", "CallFunction", "SetActorTransform"):
        assert rpc_name not in deprecated
        assert "deprecated" not in (_wrapper_for(tw, rpc_name).__doc__ or ""), rpc_name


def _unsent(monkeypatch, module, rpc_name):
    """Stub the RPC behind a wrapper, so calling it sends nothing and needs no sim."""
    async def nothing(**kwargs):
        return None

    monkeypatch.setattr(module, "_" + _wrapper_for(module, rpc_name).__name__, nothing)
    monkeypatch.setattr(module, "run_async", lambda coroutine: coroutine.close())


def test_a_deprecated_wrapper_warns_on_every_call(monkeypatch):
    import asyncio
    import warnings

    import tempo_sim.tempo_world as tw

    _unsent(monkeypatch, tw, "SetBoolProperty")
    for _ in range(2):
        with pytest.warns(DeprecationWarning, match="set_bool_property is deprecated") as caught:
            tw.set_bool_property(actor="A", property="bHidden", value=True)
        # Attributed to the caller's line, not to the wrapper or curio's dispatch.
        assert caught[0].filename == __file__

    async def awaited():
        await tw.set_bool_property(actor="A", property="bHidden", value=True)

    with pytest.warns(DeprecationWarning, match="set_bool_property is deprecated") as caught:
        asyncio.run(awaited())
    assert caught[0].filename == __file__

    _unsent(monkeypatch, tw, "SetProperty")
    with warnings.catch_warnings():
        warnings.simplefilter("error", DeprecationWarning)
        tw.set_property(actor="A", property="bHidden")


def test_the_deprecated_batch_warns_when_created():
    import warnings

    import tempo_sim.tempo_world as tw

    for make in (tw.Batch, tw.batch):
        with pytest.warns(DeprecationWarning, match="Batch is deprecated") as caught:
            staged = make()
        assert caught[0].filename == __file__
        assert staged.set_bool_property(actor="A", property="bHidden", value=True) is staged
        assert len(staged._ops) == 1
    assert MIGRATION in tw.Batch.__doc__

    # The builder for CallFunction, which is not deprecated, stays quiet.
    with warnings.catch_warnings():
        warnings.simplefilter("error", DeprecationWarning)
        tw.call(actor="A", function="F")


def test_the_breaking_message_changes():
    import tempo_sim.TempoCore.Geometry_pb2 as Geometry
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    # The property and function RPCs are always Unreal-native, so there is nothing to choose.
    for request in (WorldControl.GetPropertyRequest, WorldControl.GetPropertiesRequest,
                    WorldControl.SetPropertyRequest, WorldControl.CallFunctionRequest):
        assert "native_units" not in request.DESCRIPTOR.fields_by_name, request.__name__
    value_fields = WorldControl.Value.DESCRIPTOR.fields_by_name
    assert "transform_value" not in value_fields and "native_transform_value" in value_fields

    # A transform carries its own scale, which an unset field tells apart from a zero one.
    transform = Geometry.Transform()
    assert not transform.HasField("scale")
    transform.scale.x = 2.0
    assert transform.HasField("scale")

    # A quaternion's components are doubles, and a color's alpha can be left unset.
    assert WorldControl.Quat(x=0.1).x == 0.1
    color = WorldControl.Color(r=1)
    assert not color.HasField("a")
