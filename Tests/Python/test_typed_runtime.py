# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the runtime that serves the generated typed classes.

Attribute access is the API, so these check what an access actually does: which address it sends,
what it refuses, and how it explains a refusal. No sim: the two functions that reach the wire are
replaced, and the rest is real -- the generated package, the metadata, the Value conversion.

    pytest -m codegen Tests/Python
"""

import dataclasses
import math
import os
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GENERATOR = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python", "gen_py_classes.py")
RUNTIME_API = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python", "API")
FIXTURE = os.path.join(HERE, "fixtures", "mini_schema.json")

pytestmark = pytest.mark.codegen

WIDGET_PATH = "/Script/Engine.Widget"


def _tempo_sim_modules():
    return {name: module for name, module in list(sys.modules.items())
            if name == "tempo_sim" or name.startswith("tempo_sim.")}


@pytest.fixture(scope="module")
def classes(tmp_path_factory):
    """The fixture schema generated and importable, with the runtime registered against it.

    The runtime from the *working tree*, deliberately and with a check. conftest imports tempo_sim
    at collection time to decide what to ignore, so on any machine that has packaged the project --
    which installs the wheel -- tempo_sim is already in sys.modules from site-packages, and no
    later sys.path insertion can displace it. These tests exist to check the working tree, so the
    already-imported modules are set aside and re-imported from it, and restored afterwards for the
    sim-facing groups.

    Without this they passed against the installed wheel, silently. That is how a defect in the
    BlueprintSetter write path survived: the fix was in the tree and the test read the wheel.
    """
    out = tmp_path_factory.mktemp("runtime")
    subprocess.run([sys.executable, GENERATOR, FIXTURE, str(out / "fixture_classes")],
                   check=True, capture_output=True, text=True)
    installed = _tempo_sim_modules()
    for name in installed:
        del sys.modules[name]
    sys.path.insert(0, str(out))
    sys.path.insert(0, RUNTIME_API)
    try:
        import fixture_classes  # noqa: F401  (importing is what registers it)
        import fixture_classes.Engine as engine
        import tempo_sim.unreal as runtime
        assert runtime.__file__.startswith(RUNTIME_API), (
            "these tests must exercise the working tree, not an installed wheel; got {}"
            .format(runtime.__file__))
        yield engine
    finally:
        sys.path.remove(str(out))
        sys.path.remove(RUNTIME_API)
        for name in _tempo_sim_modules():
            del sys.modules[name]
        sys.modules.update(installed)


@pytest.fixture
def wire(monkeypatch):
    """Replace the two functions that reach the server, and record what they were asked to do."""
    import tempo_sim.unreal as unreal

    calls = {"reads": [], "writes": []}

    def fake_read(target, address):
        calls["reads"].append((target["actor"], target["component"], address))
        return "read-back"

    def fake_write(target, address, member, value):
        calls["writes"].append((target["actor"], target["component"], address, value))

    monkeypatch.setattr(unreal, "_read", fake_read)
    monkeypatch.setattr(unreal, "_write", fake_write)
    return calls


def test_reading_a_property_addresses_it_by_name(classes, wire):
    widget = classes.Widget("MyActor")
    assert widget.bEnabled == "read-back"
    assert wire["reads"] == [("MyActor", "", "bEnabled")]


def test_writing_a_property_sends_the_value(classes, wire):
    widget = classes.Widget("MyActor")
    widget.bEnabled = True
    assert wire["writes"] == [("MyActor", "", "bEnabled", True)]


def test_component_handles_carry_their_component(classes, wire):
    widget = classes.Widget("MyActor", "Mesh")
    widget.bEnabled = False
    assert wire["writes"] == [("MyActor", "Mesh", "bEnabled", False)]


@pytest.fixture
def requests(monkeypatch, classes):
    """Every request the runtime builds, as the keyword arguments it hands the generated client."""
    import tempo_sim.unreal as unreal
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    sent = []

    class FakeWorld:
        @staticmethod
        def get_property(**kwargs):
            sent.append(("get_property", kwargs))
            return WorldControl.Value(bool_value=True)

        @staticmethod
        def get_properties(**kwargs):
            sent.append(("get_properties", kwargs))

            class Response:
                values = []
            return Response()

        @staticmethod
        def set_property(**kwargs):
            sent.append(("set_property", kwargs))

        @staticmethod
        def call_function(**kwargs):
            sent.append(("call_function", kwargs))

            class Response:
                results = []
            return Response()

        @staticmethod
        def run_batch(ops, stop_on_failure):
            sent.append(("run_batch", list(ops)))

            class Response:
                results = [WorldControl.BatchOpResult() for _ in ops]
            return Response()

    monkeypatch.setattr(unreal, "_world", lambda: FakeWorld)
    import tempo_sim.world as world
    monkeypatch.setattr(world, "_world", lambda: FakeWorld)
    return sent


def test_a_subsystem_is_addressed_by_its_class_alone(classes, requests):
    subsystem = classes.Widget(subsystem="/Script/Engine.Widget")
    assert subsystem.bEnabled is True
    subsystem.bEnabled = False
    subsystem.Refresh()
    for _, kwargs in requests:
        assert kwargs["subsystem"] == "/Script/Engine.Widget"
        # Not merely empty: a subsystem request names no actor, so the server cannot mistake it.
        assert "actor" not in kwargs and "component" not in kwargs
    assert [name for name, _ in requests] == ["get_property", "set_property", "call_function"]


def test_a_batched_subsystem_op_carries_the_subsystem(classes, requests):
    from tempo_sim import world

    subsystem = classes.Widget(subsystem="/Script/Engine.Widget")
    with world.batch():
        subsystem.bEnabled = True
        subsystem.Refresh()
    (_, ops), = requests
    assert [op.set.subsystem or op.call.subsystem for op in ops] == ["/Script/Engine.Widget"] * 2
    assert all(not (op.set.actor or op.call.actor) for op in ops)


def test_get_subsystem_resolves_it_and_types_the_handle(classes, requests):
    from tempo_sim import world

    subsystem = world.get_subsystem(classes.Widget)
    assert isinstance(subsystem, classes.Widget)
    assert subsystem.subsystem == classes.Widget.__tempo_class__
    # Asked once, up front, so a subsystem that declined to exist fails at the lookup.
    assert requests == [("get_properties", {"subsystem": classes.Widget.__tempo_class__,
                                            "properties": [], "native_units": True})]


def test_a_subsystem_cannot_be_held_by_a_reference_property(classes):
    import tempo_sim.unreal as unreal

    subsystem = classes.Widget(subsystem="/Script/Engine.Widget")
    with pytest.raises(TypeError, match="subsystem"):
        unreal._to_value("actor_value", subsystem)


def test_unknown_property_suggests_a_near_miss(classes, wire):
    widget = classes.Widget("MyActor")
    # The common case is a property that was renamed, so the error should point at the new name
    # rather than just denying the old one.
    with pytest.raises(AttributeError) as caught:
        _ = widget.bEnabledd
    assert "bEnabled" in str(caught.value)
    assert wire["reads"] == [], "an unknown property must not reach the server"


def test_writing_a_read_only_property_is_refused(classes, wire):
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError, match="read-only"):
        widget.Offset = None
    assert wire["writes"] == [], "a read-only property must not reach the server"


def test_struct_property_is_addressed_member_by_member(classes, wire):
    widget = classes.Widget("MyActor")
    # A struct has no single Value that can carry it, but the server resolves a nested address, so
    # the client accumulates the path.
    widget.Settings.Scale = 2.0
    assert wire["writes"] == [("MyActor", "", "Settings.Scale", 2.0)]
    assert widget.Settings.Scale == "read-back"
    assert wire["reads"] == [("MyActor", "", "Settings.Scale")]


def test_an_inexpressible_struct_cannot_be_assigned_whole(classes, wire):
    widget = classes.Widget("MyActor")
    # FWidgetLimits holds a map, so no one Value carries it; it is reachable member by member only.
    with pytest.raises(AttributeError, match="members"):
        widget.Limits = None
    assert wire["writes"] == []


def test_an_expressible_struct_is_assigned_whole(classes, wire):
    import fixture_classes.structs as structs

    widget = classes.Widget("MyActor")
    settings = structs.FWidgetSettings(Scale=2.0)
    widget.Settings = settings
    assert wire["writes"] == [("MyActor", "", "Settings", settings)]


def test_reading_an_expressible_struct_still_returns_a_live_proxy(classes, wire):
    """A copy would turn `widget.Settings.Scale = 2.0` into a write to nothing."""
    import tempo_sim.unreal as unreal

    widget = classes.Widget("MyActor")
    assert isinstance(widget.Settings, unreal.StructProxy)
    widget.Settings.Inner.Offset = unreal.FVector(1.0, 2.0, 3.0)
    assert wire["writes"] == [
        ("MyActor", "", "Settings.Inner.Offset", unreal.FVector(1.0, 2.0, 3.0))]


def test_a_member_nothing_can_read_is_not_offered(classes, wire):
    """An array of vectors has no Value variant and is not a struct to reach into."""
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError):
        _ = widget.Waypoints
    assert wire["reads"] == []


def test_unknown_struct_member_is_refused(classes, wire):
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError) as caught:
        widget.Settings.Scal = 1.0
    assert "Scale" in str(caught.value)


def test_inherited_properties_resolve_through_super(classes):
    import tempo_sim.unreal as unreal

    registry = unreal.registered_classes()
    # Widget declares bEnabled; Object declares nothing. Each class records only what it declares,
    # so finding anything inherited means walking `super`.
    assert registry.find_property(WIDGET_PATH, "bEnabled") is not None
    assert registry.find_property(WIDGET_PATH, "NotAProperty") is None
    assert registry.is_subclass(WIDGET_PATH, "/Script/CoreUObject.Object")
    assert not registry.is_subclass("/Script/CoreUObject.Object", WIDGET_PATH)


def test_is_a_consults_the_schema_not_the_python_hierarchy(classes):
    import fixture_classes.CoreUObject as core

    widget = classes.Widget("MyActor")
    # The generated classes are flat at runtime -- Unreal's inheritance has cycles across packages
    # -- so isinstance cannot answer this and is_a goes to the schema instead.
    assert widget.is_a(classes.Widget)
    assert widget.is_a(core.Object)
    assert widget.is_a("/Script/CoreUObject.Object")
    assert not isinstance(widget, core.Object), "runtime hierarchy is intentionally flat"


def test_is_a_answers_about_the_actor_not_the_handle(classes):
    """A query for a base class hands back handles typed as that base.

    That is what makes the call site type-check, but it leaves the handle's own type saying less
    than the caller knows -- so is_a has to answer from the class the thing really has, which the
    handle carries, or filtering a broad query down to one class could never work.
    """
    import fixture_classes.CoreUObject as core

    as_base = core.Object("MyActor", class_path=WIDGET_PATH)
    assert as_base.is_a(classes.Widget), "the actor is a Widget however the handle is typed"
    assert as_base.is_a(core.Object)

    # Nothing recorded, so the handle's own type is the best anyone here knows.
    assert not core.Object("MyActor").is_a(classes.Widget)


def test_metadata_names_the_value_variant(classes):
    import tempo_sim.unreal as unreal

    registry = unreal.registered_classes()
    # Python's types cannot pick a variant: a str could be a string, an enum, a class, an asset, an
    # actor or a component. The metadata has to say.
    assert registry.find_property(WIDGET_PATH, "bEnabled")["variant"] == "bool_value"
    assert registry.find_property(WIDGET_PATH, "Tags")["variant"] == "string_array_value"
    assert registry.find_property(WIDGET_PATH, "Mode")["variant"] == "enum_value"


@pytest.mark.parametrize("variant,value", [
    ("bool_value", True),
    ("int_value", 7),
    ("int64_value", 1 << 40),
    ("float_value", 1.5),
    ("string_value", "hello"),
    ("string_array_value", ["a", "b"]),
    ("int_array_value", [1, 2, 3]),
])
def test_scalar_and_container_values_round_trip(classes, variant, value):
    import tempo_sim.unreal as unreal
    assert unreal._from_value(unreal._to_value(variant, value)) == value


def test_structured_values_round_trip(classes):
    import tempo_sim.unreal as unreal

    # Values a 32-bit float holds exactly where the wire is 32-bit, so equality is exact.
    for variant, original in (
            ("vector_value", unreal.FVector(1.0, 2.0, 3.0)),
            ("vector2d_value", unreal.FVector2D(4.0, 5.0)),
            ("int_vector_value", unreal.FIntVector(1, 2, 3)),
            ("int_point_value", unreal.FIntPoint(6, 7)),
            ("rotator_value", unreal.FRotator(10.0, 20.0, 30.0)),
            ("quat_value", unreal.FQuat(0.0, 0.0, 0.5, 0.5)),
            ("color_value", unreal.FColor(1, 2, 3, 4)),
            ("linear_color_value", unreal.FLinearColor(0.25, 0.5, 0.75, 0.125)),
            ("native_transform_value", unreal.FTransform(unreal.FQuat(0.0, 0.0, 0.5, 0.5),
                                                         unreal.FVector(1.0, 2.0, 3.0),
                                                         unreal.FVector(2.0, 2.0, 2.0)))):
        assert unreal._from_value(unreal._to_value(variant, original)) == original, variant


def test_value_types_have_unreals_shapes(classes):
    """Constructed in Unreal's own order, and carried by name rather than by position."""
    import tempo_sim.unreal as unreal

    rotator = unreal.FRotator(10.0, 20.0, 30.0)
    assert (rotator.Pitch, rotator.Yaw, rotator.Roll) == (10.0, 20.0, 30.0)
    wire = unreal._to_value("rotator_value", rotator).rotator_value
    assert (wire.r, wire.p, wire.y) == (30.0, 10.0, 20.0), "r/p/y are roll, pitch and yaw"

    identity = unreal.FTransform()
    assert identity.Rotation == unreal.FQuat(0.0, 0.0, 0.0, 1.0)
    assert identity.Scale3D == unreal.FVector(1.0, 1.0, 1.0), "Unreal's default scale is 1"
    assert unreal.FColor().A == 255 and unreal.FLinearColor().A == 1.0, "opaque by default"


def _flat(value):
    """A value type's fields in order, a nested one's spliced in, for an approximate comparison."""
    return [leaf for item in dataclasses.astuple(value)
            for leaf in (item if isinstance(item, tuple) else (item,))]


def test_conversions_apply_the_servers_rule(classes):
    # The expected numbers are TempoConversion.h's: cm -> m with Y negated for a length, degrees ->
    # radians with pitch and yaw negated for a rotation, and a direction's handedness alone.
    import tempo_sim.unreal as unreal
    from tempo_sim.conversion import (quat_of, si_direction_to_ue, si_to_ue,
                                      ue_direction_to_si, ue_to_si)

    assert ue_to_si(250.0) == 2.5 and si_to_ue(2.5) == 250.0, "a bare number is a length"
    v = ue_to_si(unreal.FVector(100.0, 50.0, -25.0))
    assert (v.x, v.y, v.z) == (1.0, -0.5, -0.25)
    v2 = ue_to_si(unreal.FVector2D(100.0, 50.0))
    assert (v2.x, v2.y) == (1.0, -0.5)
    r = ue_to_si(unreal.FRotator(Pitch=45.0, Yaw=180.0, Roll=90.0))
    assert (r.r, r.p, r.y) == pytest.approx((math.pi / 2, -math.pi / 4, -math.pi))
    q = ue_to_si(quat_of(unreal.FRotator(Yaw=90.0)))
    assert (q.r, q.p, q.y) == pytest.approx((0.0, 0.0, -math.pi / 2))
    t = ue_to_si(unreal.FTransform(quat_of(unreal.FRotator(Yaw=90.0)),
                                   unreal.FVector(100.0, 50.0, 0.0), unreal.FVector(2.0, 2.0, 2.0)))
    assert (t.location.x, t.location.y, t.rotation.y) == pytest.approx((1.0, -0.5, -math.pi / 2))

    d = ue_direction_to_si(unreal.FVector(0.0, 1.0, 0.0))
    assert (d.x, d.y, d.z) == (0.0, -1.0, 0.0)
    assert si_direction_to_ue(d) == unreal.FVector(0.0, 1.0, 0.0)

    for value in (unreal.FVector(1.0, 2.0, 3.0), unreal.FVector2D(4.0, 5.0),
                  unreal.FRotator(10.0, 20.0, 30.0),
                  # Scale 1: TempoCore.Transform carries none, so a round trip comes back with 1.
                  unreal.FTransform(quat_of(unreal.FRotator(4.0, 5.0, 6.0)),
                                    unreal.FVector(1.0, 2.0, 3.0))):
        back = si_to_ue(ue_to_si(value))
        assert type(back) is type(value)
        assert _flat(back) == pytest.approx(_flat(value)), (value, back)


def test_the_rotation_math_is_unreals(classes):
    """quat_of and rotator_of reproduce FRotator::Quaternion and FQuat::Rotator, singularities
    included, so a transform's rotation means what it does in the sim."""
    import tempo_sim.unreal as unreal
    from tempo_sim.conversion import quat_of, rotator_of

    quarter = math.sqrt(0.5)
    assert _flat(quat_of(unreal.FRotator(Yaw=90.0))) == pytest.approx([0.0, 0.0, quarter, quarter])
    assert _flat(quat_of(unreal.FRotator(Pitch=90.0))) == pytest.approx([0.0, -quarter, 0.0, quarter])
    for rotator in (unreal.FRotator(10.0, 20.0, 30.0), unreal.FRotator(-45.0, 170.0, -120.0),
                    unreal.FRotator(0.0, -179.0, 0.0), unreal.FRotator(89.9, 10.0, 5.0)):
        assert _flat(rotator_of(quat_of(rotator))) == pytest.approx(_flat(rotator)), rotator
    # At the pole Unreal folds roll into yaw, and so does this.
    assert _flat(rotator_of(quat_of(unreal.FRotator(90.0, 30.0, 0.0)))) == pytest.approx(
        [90.0, 30.0, 0.0])


def test_conversions_refuse_what_they_cannot_convert(classes):
    import tempo_sim.TempoCore.Geometry_pb2 as Geometry
    import tempo_sim.unreal as unreal
    from tempo_sim.conversion import si_to_ue, ue_to_si

    # No TempoCore counterpart, and an integer vector cannot be scaled to meters anyway.
    for value in (unreal.FIntVector(), unreal.FIntPoint(), unreal.FColor(), unreal.FLinearColor(),
                  True):
        with pytest.raises(TypeError, match="ue_to_si"):
            ue_to_si(value)
    with pytest.raises(TypeError, match="si_to_ue"):
        si_to_ue(unreal.FVector())
    with pytest.raises(TypeError, match="ue_to_si"):
        ue_to_si(Geometry.Vector())


@pytest.fixture
def world_calls(monkeypatch, classes):
    """Every tempo_world call `world` makes, as (name, keyword arguments), answered with stand-ins."""
    import tempo_sim.world as world
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    sent = []

    class FakeWorld:
        def __getattr__(self, name):
            def call(**kwargs):
                sent.append((name, kwargs))
                if name == "add_component":
                    return WorldControl.AddComponentResponse(name=kwargs["name"] + "_0")
                if name == "get_all_components":
                    return WorldControl.GetAllComponentsResponse(components=[
                        WorldControl.ComponentDescriptor(actor=kwargs["actor"], name="Mesh",
                                                         class_path="/Script/Engine.MeshThing"),
                        WorldControl.ComponentDescriptor(actor=kwargs["actor"], name="Other",
                                                         class_path="/Script/Engine.Widget")])
                return None
            return call

    monkeypatch.setattr(world, "_world", lambda: FakeWorld())
    return sent


def test_the_world_mirror_sends_names_for_handles(classes, world_calls):
    """world is tempo_world given handles: each sends the names tempo_world takes, SI untouched."""
    import tempo_sim.unreal as unreal
    import tempo_sim.world as world

    actor = unreal.Object("Truck")
    other = unreal.Object("Marker")
    component = unreal.Object("Truck", "Body")
    location = world.Vector(x=1.0)

    world.destroy_actor(actor)
    world.set_actor_location(actor, location, relative_to_actor=other)
    world.set_component_rotation(component, world.Rotation(y=0.5), relative_to_world=True)
    world.get_component_transform(component, relative_to_component=unreal.Object("Truck", "Root"))
    world.raycast(world.Vector(), location, ignored_actors=[actor, other])
    assert world_calls == [
        ("destroy_actor", {"actor": "Truck"}),
        ("set_actor_location", {"actor": "Truck", "location": location,
                                "relative_to_actor": "Marker"}),
        ("set_component_rotation", {"actor": "Truck", "component": "Body",
                                    "rotation": world.Rotation(y=0.5), "relative_to_world": True}),
        ("get_component_transform", {"actor": "Truck", "component": "Body", "frame": 0,
                                     "relative_to_component": "Root", "socket": ""}),
        ("raycast", {"start": world.Vector(), "end": location, "collision_channel": 0,
                     "ignored_actors": ["Truck", "Marker"]}),
    ]


def test_the_world_mirror_refuses_the_wrong_kind_of_handle(classes, world_calls):
    """A component where an actor is wanted, or the other way round, fails before anything is sent."""
    import tempo_sim.unreal as unreal
    import tempo_sim.world as world

    with pytest.raises(TypeError, match="must be an actor"):
        world.destroy_actor(unreal.Object("Truck", "Body"))
    with pytest.raises(TypeError, match="must be a component"):
        world.destroy_component(unreal.Object("Truck"))
    with pytest.raises(TypeError, match="relative_to_actor must be an actor"):
        world.set_actor_location(unreal.Object("Truck"), world.Vector(),
                                 relative_to_actor=unreal.Object("Truck", "Body"))
    assert world_calls == []


def test_adding_and_finding_components_is_typed(classes, world_calls):
    import tempo_sim.unreal as unreal
    import tempo_sim.world as world

    actor = unreal.Object("Truck")
    added = world.add_component(classes.MeshThing, actor, name="Light",
                                parent=unreal.Object("Truck", "Body"))
    assert type(added) is classes.MeshThing
    assert (added.actor, added.component) == ("Truck", "Light_0"), "the name the sim gave it"
    assert world_calls[0] == ("add_component", {
        "component_type": "/Script/Engine.MeshThing", "actor": "Truck", "name": "Light",
        "parent": "Body", "transform": None, "socket": ""})

    found = world.get_components(actor, classes.MeshThing)
    assert [(type(c), c.component) for c in found] == [(classes.MeshThing, "Mesh")], \
        "only the components that are MeshThings, by their real class"


def test_enum_values_travel_as_their_authored_name(classes):
    import tempo_sim.unreal as unreal
    import fixture_classes.enums as enums

    # The wire carries the authored name, which is what makes a generated enum str-valued.
    assert unreal._to_value("enum_value", enums.EWidgetMode.Advanced).enum_value == "Advanced"
    assert unreal._to_value("enum_value", "Simple").enum_value == "Simple"


def test_a_class_travels_as_its_path(classes):
    import tempo_sim.unreal as unreal
    # A generated class stands for the Unreal class it came from, and a path is unambiguous where a
    # name is not.
    assert unreal._to_value("class_value", classes.Widget).class_value == WIDGET_PATH


def test_an_object_handle_travels_as_its_address(classes):
    import tempo_sim.unreal as unreal
    assert unreal._to_value("actor_value", classes.Widget("Thing")).actor_value == "Thing"
    assert unreal._to_value("component_value",
                            classes.Widget("Thing", "Mesh")).component_value == "Thing:Mesh"


# -- batching and snapshots ----------------------------------------------------------------------


@pytest.fixture
def fake_world(monkeypatch, classes):
    """Replace the generated client wrappers, recording the calls made through them.

    One level lower than the `wire` fixture: these tests are about how many calls are made and in
    what order, so the calls themselves have to be visible.
    """
    import tempo_sim.unreal as unreal
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    calls = []
    # Index -> (code, error) for an op the fake should fail. Later ops come back ABORTED, as the
    # server reports them under stop_on_failure.
    failures = {}

    def describe(op):
        kind = op.WhichOneof("op")
        request = getattr(op, kind)
        return "{} {}".format(kind, request.function if kind == "call" else request.property)

    class FakeWorld:
        @staticmethod
        def run_batch(ops, stop_on_failure):
            calls.append(("run_batch", [describe(op) for op in ops]))
            assert stop_on_failure is True, "the typed runtime stops at the first failure"
            results, failed = [], False
            for index, op in enumerate(ops):
                result = WorldControl.BatchOpResult()
                if failed:
                    result.code, result.error = 10, "not run"
                elif index in failures:
                    result.code, result.error = failures[index]
                    failed = True
                elif op.WhichOneof("op") == "get":
                    result.value.bool_value = True
                elif op.WhichOneof("op") == "call":
                    result.call.results.add(name="ReturnValue",
                                            typed_value=WorldControl.Value(bool_value=True))
                results.append(result)

            class Response:
                pass
            Response.results = results
            return Response()

        @staticmethod
        def set_property(**kwargs):
            calls.append(("set_property", kwargs["property"]))

        @staticmethod
        def call_function(**kwargs):
            calls.append(("call_function", kwargs["function"]))

        @staticmethod
        def get_property(**kwargs):
            calls.append(("get_property", kwargs["property"]))
            import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl
            return WorldControl.Value(bool_value=True)

        @staticmethod
        def get_properties(**kwargs):
            calls.append(("get_properties", list(kwargs["properties"])))
            import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

            class Response:
                values = [WorldControl.NamedValue(name=name, value=WorldControl.Value(bool_value=True))
                          for name in kwargs["properties"]]
            return Response()

    monkeypatch.setattr(unreal, "_world", lambda: FakeWorld)
    FakeWorld.failures = failures
    return calls


def test_a_batch_sends_one_call(classes, fake_world):
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with world.batch():
        widget.bEnabled = True
        widget.Mode = "Simple"
        widget.Tags = ["x"]

    assert fake_world == [("run_batch", ["set bEnabled", "set Mode", "set Tags"])], \
        "three writes in a batch should be one call"


def test_an_accessor_write_travels_in_the_same_batch(classes, fake_world):
    """A BlueprintSetter write is a call, which SetProperties could not carry: it split the batch."""
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with world.batch():
        widget.bEnabled = True
        widget.ScaleFactor = 2.0
        widget.Tags = ["x"]
    assert fake_world == [("run_batch", ["set bEnabled", "call SetScaleFactor", "set Tags"])]


def test_a_deferred_read_rides_along(classes, fake_world):
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with world.batch() as b:
        widget.bEnabled = False
        enabled = b.get(lambda: widget.bEnabled)
        assert not enabled.done
        with pytest.raises(RuntimeError, match="not run yet"):
            _ = enabled.value
        resized = b.call(lambda: widget.Resize(4))
    assert fake_world == [("run_batch", ["set bEnabled", "get bEnabled", "call Resize"])], \
        "the read and the call go out with the write, after it, in one call"
    assert enabled.value is True
    assert resized.value is True


def test_an_immediate_read_sends_what_is_queued_with_it(classes, fake_world):
    """Otherwise it would reach the server ahead of the writes before it, and see stale values."""
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with world.batch():
        widget.bEnabled = True
        assert widget.Mode is True
        assert fake_world == [("run_batch", ["set bEnabled", "get Mode"])]
        widget.Tags = ["x"]
    assert fake_world[-1] == ("run_batch", ["set Tags"])


def test_an_immediate_call_sends_what_is_queued_with_it(classes, fake_world):
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with world.batch():
        widget.bEnabled = True
        assert widget.Resize(4) is True
    assert fake_world == [("run_batch", ["set bEnabled", "call Resize"])]


def test_a_failed_op_is_named_and_what_follows_it_did_not_run(classes, fake_world):
    from tempo_sim import world
    import tempo_sim.unreal as unreal

    unreal._world().failures[1] = (9, "Property 'Mode' has the wrong type")
    widget = classes.Widget("MyActor")
    with pytest.raises(unreal.BatchError) as caught:
        with world.batch() as b:
            widget.bEnabled = True
            widget.Mode = "Simple"
            later = b.get(lambda: widget.bEnabled)
    assert caught.value.index == 1, "the op that failed, not one the server then skipped"
    assert "Mode" in str(caught.value)
    with pytest.raises(unreal.BatchError):
        _ = later.value


def test_a_deferred_lambda_cannot_transform_what_it_reads(classes, fake_world):
    """There is no value yet inside the lambda, so working on one has to fail rather than be skipped."""
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with pytest.raises(TypeError):
        with world.batch() as b:
            b.get(lambda: widget.ScaleFactor * 2)


def test_a_deferred_lambda_makes_exactly_one_request(classes, fake_world):
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with pytest.raises(TypeError, match="exactly one"):
        with world.batch() as b:
            b.get(lambda: (widget.bEnabled, widget.Mode))


def test_writes_outside_a_batch_are_sent_immediately(classes, fake_world):
    widget = classes.Widget("MyActor")
    widget.bEnabled = True
    widget.Mode = "Simple"
    assert fake_world == [("set_property", "bEnabled"), ("set_property", "Mode")]


def test_a_failed_block_sends_nothing(classes, fake_world):
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with pytest.raises(ValueError):
        with world.batch():
            widget.bEnabled = True
            raise ValueError("abandoned")
    assert fake_world == [], "an abandoned block must not flush"


def test_nested_batches_flush_once(classes, fake_world):
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    outer = world.batch()
    with outer:
        widget.bEnabled = True
        with outer:
            widget.Mode = "Simple"
        assert fake_world == [], "the inner block must not flush early"
    assert fake_world == [("run_batch", ["set bEnabled", "set Mode"])]


def test_a_snapshot_reads_once_and_serves_the_rest(classes, fake_world):
    widget = classes.Widget("MyActor")
    with widget.snapshot("bEnabled", "Mode"):
        assert widget.bEnabled is True
        assert widget.bEnabled is True
        assert widget.Mode is True
    assert fake_world == [("get_properties", ["bEnabled", "Mode"])], \
        "reads inside a snapshot should not reach the server"


def test_reads_outside_a_snapshot_go_to_the_server(classes, fake_world):
    widget = classes.Widget("MyActor")
    assert widget.bEnabled is True
    assert widget.bEnabled is True
    assert fake_world == [("get_property", "bEnabled"), ("get_property", "bEnabled")]


def test_a_snapshot_with_no_names_fetches_the_readable_properties(classes, fake_world):
    widget = classes.Widget("MyActor")
    with widget.snapshot():
        pass
    assert len(fake_world) == 1
    kind, names = fake_world[0]
    assert kind == "get_properties"
    # Every property carryable as one value, and nothing that is only addressable through members.
    assert set(names) >= {"bEnabled", "Mode", "Tags"}
    assert "Settings" not in names, "a struct has nothing to prefetch"


def test_writing_inside_a_snapshot_drops_the_stale_entry(classes, fake_world):
    widget = classes.Widget("MyActor")
    with widget.snapshot("bEnabled"):
        assert widget.bEnabled is True          # served from the snapshot
        widget.bEnabled = False                 # the snapshot is now stale for this property
        _ = widget.bEnabled                     # so this must ask the server
    assert fake_world == [
        ("get_properties", ["bEnabled"]),
        ("set_property", "bEnabled"),
        ("get_property", "bEnabled"),
    ]


def test_the_snapshot_is_released_on_exit(classes, fake_world):
    widget = classes.Widget("MyActor")
    with widget.snapshot("bEnabled"):
        _ = widget.bEnabled
    _ = widget.bEnabled
    assert fake_world[-1] == ("get_property", "bEnabled"), "a read after the block is not cached"


# -- calling functions ----------------------------------------------------------------------------


@pytest.fixture
def fake_calls(monkeypatch, classes):
    """Record CallFunction invocations and hand back a chosen set of results."""
    import tempo_sim.unreal as unreal
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    recorded = {"calls": [], "results": []}

    class FakeWorld:
        @staticmethod
        def call_function(**kwargs):
            recorded["calls"].append(kwargs)

            class Response:
                results = recorded["results"]
            return Response()

    monkeypatch.setattr(unreal, "_world", lambda: FakeWorld)

    def result(name, **value_kwargs):
        return WorldControl.FunctionResult(name=name,
                                          typed_value=WorldControl.Value(**value_kwargs))

    recorded["make"] = result
    return recorded


def test_a_function_is_callable_through_the_handle(classes, fake_calls):
    widget = classes.Widget("MyActor")
    widget.Refresh()
    assert len(fake_calls["calls"]) == 1
    call = fake_calls["calls"][0]
    assert call["function"] == "Refresh"
    assert call["actor"] == "MyActor"
    assert list(call["args"]) == []
    # The typed API is Unreal-native, and a function argument is the same Value a property takes.
    assert call["native_units"] is True


def test_arguments_may_be_positional_or_named(classes, fake_calls):
    widget = classes.Widget("MyActor")
    widget.Resize(4)
    widget.Resize(NewSize=5)
    assert [arg["name"] for arg in fake_calls["calls"][0]["args"]] == ["NewSize"]
    assert fake_calls["calls"][0]["args"][0]["value"].int_value == 4
    assert fake_calls["calls"][1]["args"][0]["value"].int_value == 5


def test_a_return_value_comes_back_typed(classes, fake_calls):
    fake_calls["results"] = [fake_calls["make"]("ReturnValue", bool_value=True)]
    widget = classes.Widget("MyActor")
    # Not a string to be parsed: the same representation a property read returns.
    assert widget.Resize(4) is True


def test_a_void_function_returns_none(classes, fake_calls):
    fake_calls["results"] = []
    widget = classes.Widget("MyActor")
    assert widget.Refresh() is None


def test_out_parameters_come_back_by_name(classes, fake_calls):
    # How a Blueprint usually hands back more than one thing. Position would be guesswork, so the
    # result is keyed.
    fake_calls["results"] = [
        fake_calls["make"]("ReturnValue", bool_value=True),
        fake_calls["make"]("Remainder", int_value=2),
    ]
    widget = classes.Widget("MyActor")
    assert widget.Resize(4) == {"ReturnValue": True, "Remainder": 2}


@pytest.mark.parametrize("case,call", [
    ("too many positional", lambda w: w.Resize(1, 2)),
    ("unknown name", lambda w: w.Resize(NotAParameter=1)),
    ("missing argument", lambda w: w.Resize()),
    ("duplicated argument", lambda w: w.Resize(1, NewSize=2)),
])
def test_bad_arguments_are_refused_before_the_wire(classes, fake_calls, case, call):
    widget = classes.Widget("MyActor")
    with pytest.raises(TypeError):
        call(widget)
    assert fake_calls["calls"] == [], "{} must not reach the server".format(case)


def test_an_uncallable_function_is_not_offered(classes, fake_calls):
    widget = classes.Widget("MyActor")
    # Latent in the fixture, so the server would refuse it; the client does not pretend otherwise.
    with pytest.raises(AttributeError):
        widget.Latent()


def test_a_function_cannot_be_assigned_to(classes, fake_calls):
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError, match="is a function"):
        widget.Refresh = 1


def test_an_unknown_member_suggests_either_kind(classes, fake_calls):
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError) as caught:
        widget.Refres()
    assert "Refresh" in str(caught.value)


# -- components -----------------------------------------------------------------------------------


def test_a_component_is_reachable_as_an_attribute(classes, wire):
    widget = classes.Widget("MyActor")
    mesh = widget.Mesh
    # A handle to the component on this actor, typed as the component's own class, so its members
    # are addressable in turn.
    assert mesh.actor == "MyActor"
    assert mesh.component == "Mesh"
    assert type(mesh).__tempo_class__ == "/Script/Engine.MeshThing"
    assert wire["reads"] == [], "reaching a component is not itself a read"


def test_a_components_properties_address_through_it(classes, wire):
    widget = classes.Widget("MyActor")
    widget.Mesh.Visible = True
    assert wire["writes"] == [("MyActor", "Mesh", "Visible", True)]


def test_a_component_cannot_be_assigned(classes, wire):
    widget = classes.Widget("MyActor")
    # The attribute names a component that exists; replacing one is not an assignment.
    with pytest.raises(AttributeError, match="is a component"):
        widget.Mesh = None
    assert wire["writes"] == []


def test_a_component_whose_class_is_absent_is_not_offered(classes, wire):
    widget = classes.Widget("MyActor")
    # Typing it would mean a handle whose members are unknown, which is not worth offering.
    with pytest.raises(AttributeError):
        _ = widget.Gone


def test_an_unnameable_component_is_not_offered(classes, wire):
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError):
        _ = widget.Has_Space


# -- watching a property --------------------------------------------------------------------------


def test_watch_yields_each_value(classes, monkeypatch):
    import tempo_sim.unreal as unreal
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    asked = {}

    class FakeWorld:
        @staticmethod
        def stream_property(**kwargs):
            asked.update(kwargs)
            for value in (True, False, True):
                yield WorldControl.Value(bool_value=value)

    monkeypatch.setattr(unreal, "_world", lambda: FakeWorld)

    widget = classes.Widget("MyActor")
    assert list(widget.watch("bEnabled")) == [True, False, True]
    assert asked["property"] == "bEnabled"
    assert asked["actor"] == "MyActor"
    assert asked["native_units"] is True


def test_watching_an_unknown_property_fails_immediately(classes, monkeypatch):
    import tempo_sim.unreal as unreal

    opened = []

    class FakeWorld:
        @staticmethod
        def stream_property(**kwargs):
            opened.append(kwargs)
            return iter(())

    monkeypatch.setattr(unreal, "_world", lambda: FakeWorld)

    widget = classes.Widget("MyActor")
    # Checked before the subscription opens, so a typo fails here rather than after the first tick.
    with pytest.raises(AttributeError):
        next(widget.watch("bEnabledd"))
    assert opened == []


def test_an_inexpressible_struct_cannot_be_watched(classes, monkeypatch):
    import tempo_sim.unreal as unreal
    monkeypatch.setattr(unreal, "_world", lambda: None)
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError, match="not readable as one value"):
        next(widget.watch("Limits"))


@pytest.fixture
def server(monkeypatch):
    """Replace the generated RPC module, so the real _write runs and its request is visible.

    The `wire` fixture above replaces `_write` itself, which is right for testing what an address
    resolves to and wrong for testing what `_write` sends. This one goes one layer lower.
    """
    import tempo_sim.unreal as unreal

    sent = {"set_property": [], "call_function": [], "run_batch": []}

    class FakeWorld:
        @staticmethod
        def set_property(**kwargs):
            sent["set_property"].append(kwargs)

        @staticmethod
        def call_function(**kwargs):
            sent["call_function"].append(kwargs)

        @staticmethod
        def run_batch(ops, stop_on_failure):
            import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl
            sent["run_batch"].append(list(ops))

            class Response:
                results = [WorldControl.BatchOpResult() for _ in ops]
            return Response()

    monkeypatch.setattr(unreal, "_world", lambda: FakeWorld)
    return sent


def test_a_property_with_a_setter_is_written_through_it(classes, server):
    """And the argument is named, which is the part that was wrong.

    CallFunction refuses an unnamed argument outright -- "Every arg in a CallFunction request must
    have a name" -- and matches the name against the function's own parameters. Both runtimes sent
    "" until the generator started resolving it, so every write to one of the 306 properties with a
    BlueprintSetter failed. Nothing caught it because no test wrote one against a live sim.
    """
    widget = classes.Widget("MyActor")
    widget.ScaleFactor = 2.5

    assert server["set_property"] == [], "a property with a setter must not be written directly"
    assert len(server["call_function"]) == 1
    call = server["call_function"][0]
    assert call["function"] == "SetScaleFactor"
    assert [arg["name"] for arg in call["args"]] == ["InScale"], \
        "the argument has to carry the setter's own parameter name"
    assert call["args"][0]["value"].float_value == pytest.approx(2.5)


def test_a_batched_setter_write_keeps_the_argument_name(classes, server):
    from tempo_sim import world

    widget = classes.Widget("MyActor")
    with world.batch():
        widget.ScaleFactor = 3.5
    (op,), = server["run_batch"]
    assert op.call.function == "SetScaleFactor"
    assert [arg.name for arg in op.call.args] == ["InScale"]


def test_a_property_whose_setter_cannot_be_named_is_read_only(classes):
    """Writing it directly would skip the setter, which is the thing the accessor path prevents.

    UE 5.8 has exactly one of these: UCineCameraSettings::DefaultFilmbackPreset declares
    BlueprintSetter=SetDefaultfilmbackPreset, with a lowercase f, and no such function exists.
    """
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError, match="read-only"):
        widget.Untouchable = True


# -- the drift check ------------------------------------------------------------------------------
#
# check_schema compares what each installed package recorded against what the sim reports, entry by
# entry. Per entry and not per build because a package may hold part of one by design -- the
# published classes describe the modules every build ships (TYPED_WORLD_API_PLAN.md section 16) --
# and because a single number can only ever say that something moved.

@pytest.fixture
def describing(monkeypatch, classes):
    """Let a test say what the sim reports, and check `check_schema` against it.

    The sim's assets default to exactly the installed catalog, so a test about classes is not
    also, by accident, a test about assets.
    """
    from tempo_sim import world

    class Asset:
        def __init__(self, path, class_path):
            self.path, self.class_path = path, class_path

    class Entry:
        def __init__(self, name, content_hash):
            self.name = self.class_path = name
            self.content_hash = content_hash

    class Header:
        engine_version = "5.8"
        schema_version = "3"

    def describe(classes=None, structs=None, enums=None, assets=None):
        class Response:
            header = Header()

        reported_assets = _installed_package(classes_fixture)["assets"] if assets is None else assets

        def fake_world():
            class FakeWorld:
                @staticmethod
                def describe_classes(**_):
                    response = Response()
                    response.classes = [Entry(n, h) for n, h in (classes or {}).items()]
                    response.structs = [Entry(n, h) for n, h in (structs or {}).items()]
                    response.enums = [Entry(n, h) for n, h in (enums or {}).items()]
                    return response

                @staticmethod
                def get_assets(**_):
                    class AssetsResponse:
                        pass
                    AssetsResponse.assets = [Asset(path, cls) for path, cls in reported_assets.items()]
                    return AssetsResponse
            return FakeWorld

        # `world` reaches the generated RPC module through its own `_world`, not the runtime's.
        monkeypatch.setattr(world, "_world", fake_world)

    classes_fixture = classes
    return describe


def _installed_package(classes):
    package = classes.__name__.split(".")[0]
    import tempo_sim.unreal as unreal
    return next(p for p in unreal.registered_classes().packages if p.package == package).hashes


def _installed(classes):
    """What the fixture package recorded, which is what a check compares against."""
    return _installed_package(classes)


def test_matching_hashes_pass(classes, describing):
    from tempo_sim import world

    installed = _installed(classes)
    describing(classes=installed["classes"], structs=installed["structs"],
               enums=installed["enums"])
    assert world.check_schema() is True


def test_a_changed_class_is_named(classes, describing):
    from tempo_sim import world

    installed = _installed(classes)
    reported = dict(installed["classes"])
    reported[WIDGET_PATH] = "SOMETHING-ELSE"
    describing(classes=reported, structs=installed["structs"], enums=installed["enums"])

    with pytest.raises(world.SchemaMismatch) as raised:
        world.check_schema()
    assert "1 changed" in str(raised.value)
    assert WIDGET_PATH in str(raised.value)
    # One of each kind is a "class", a "struct" or an "enum". Trimming the plural key gave
    # "classe", which is how the first real report read.
    assert "class /Script/Engine.Widget" in str(raised.value)


def test_a_class_the_sim_does_not_have_is_named_separately(classes, describing):
    """Not the same finding as a changed one: nothing can address it at all."""
    from tempo_sim import world

    installed = _installed(classes)
    reported = {path: value for path, value in installed["classes"].items()
                if path != WIDGET_PATH}
    describing(classes=reported, structs=installed["structs"], enums=installed["enums"])

    with pytest.raises(world.SchemaMismatch) as raised:
        world.check_schema()
    assert "1 not in this sim" in str(raised.value)
    assert WIDGET_PATH in str(raised.value)


def test_a_class_only_the_sim_has_is_not_a_finding(classes, describing):
    """The normal condition of a published package, which describes part of a build by design.

    This is the case a whole-walk hash cannot express: every class the package holds is current,
    and the one number still differs.
    """
    from tempo_sim import world

    installed = _installed(classes)
    reported = dict(installed["classes"])
    reported["/Game/Blueprints/BP_Thing.BP_Thing_C"] = "A-PROJECT-CLASS"
    describing(classes=reported, structs=installed["structs"], enums=installed["enums"])

    assert world.check_schema() is True


def test_a_changed_struct_or_enum_is_a_finding(classes, describing):
    from tempo_sim import world

    installed = _installed(classes)
    structs = {name: "MOVED" for name in installed["structs"]}
    describing(classes=installed["classes"], structs=structs, enums=installed["enums"])

    with pytest.raises(world.SchemaMismatch, match="struct FWidgetSettings"):
        world.check_schema()


def test_an_asset_the_sim_did_not_cook_is_named(classes, describing):
    """A catalog entry the build no longer has: moved, renamed, or simply no longer referenced."""
    from tempo_sim import world

    installed = _installed(classes)
    assets = {path: cls for path, cls in installed["assets"].items()
              if path != "/Game/Widgets/W_Main.W_Main"}
    describing(classes=installed["classes"], structs=installed["structs"],
               enums=installed["enums"], assets=assets)

    with pytest.raises(world.SchemaMismatch) as raised:
        world.check_schema()
    assert "asset /Game/Widgets/W_Main.W_Main" in str(raised.value)
    assert "not in this sim" in str(raised.value)


def test_an_asset_whose_class_changed_is_named(classes, describing):
    from tempo_sim import world

    installed = _installed(classes)
    assets = dict(installed["assets"])
    assets["/Game/Widgets/W_Main.W_Main"] = "/Script/Engine.Widget"
    describing(classes=installed["classes"], structs=installed["structs"],
               enums=installed["enums"], assets=assets)

    with pytest.raises(world.SchemaMismatch, match="1 changed"):
        world.check_schema()


def test_a_mismatch_can_warn_instead_of_raising(classes, describing):
    from tempo_sim import world

    installed = _installed(classes)
    reported = dict(installed["classes"])
    reported[WIDGET_PATH] = "SOMETHING-ELSE"
    describing(classes=reported, structs=installed["structs"], enums=installed["enums"])

    with pytest.warns(UserWarning, match="do not match this sim"):
        assert world.check_schema(raise_on_mismatch=False) is False

# -- the asset catalog -----------------------------------------------------------------------------


def test_a_catalog_entry_is_an_asset_ref(classes):
    from fixture_classes.assets import Game
    import tempo_sim.unreal as unreal

    entry = Game.Widgets.W_Main
    assert isinstance(entry, unreal.AssetRef)
    assert entry.path == "/Game/Widgets/W_Main.W_Main"
    assert entry.class_path == "/Script/Engine.MeshThing"


def test_an_asset_travels_as_its_path(classes):
    from fixture_classes.assets import Game
    import tempo_sim.unreal as unreal

    assert unreal._to_value("asset_value", Game.Widgets.W_Main).asset_value == "/Game/Widgets/W_Main.W_Main"
    # A plain string still works at run time; it is only the type checker that refuses it.
    assert unreal._to_value("asset_value", "/Game/X.X").asset_value == "/Game/X.X"


def test_an_asset_reads_back_as_an_asset_ref(classes):
    import tempo_sim.unreal as unreal
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    read = unreal._from_value(WorldControl.Value(asset_value="/Game/Widgets/W_Main.W_Main"))
    assert read == unreal.AssetRef("/Game/Widgets/W_Main.W_Main")
    assert unreal._from_value(WorldControl.Value(asset_value="")) is None, "an unset reference is None"
    assert unreal._from_value(WorldControl.Value(asset_array_value={"values": ["/A.A", "/B.B"]})) == [
        unreal.AssetRef("/A.A"), unreal.AssetRef("/B.B")]


def test_a_catalog_entry_cannot_be_changed(classes):
    from fixture_classes.assets import Game
    with pytest.raises(AttributeError):
        Game.Widgets.W_Main.path = "/Game/Elsewhere"


# -- struct values --------------------------------------------------------------------------------


def test_a_struct_value_names_its_struct_and_carries_only_what_it_was_given(classes):
    import tempo_sim.unreal as unreal
    import fixture_classes.structs as structs

    message = unreal._to_value("struct_value", structs.FWidgetSettings(Scale=2.0))
    assert message.struct_value.struct_name == "FWidgetSettings"
    # A write is a merge, so what was not given must not be sent: sending a default would clobber
    # the destination's own value.
    assert [field.name for field in message.struct_value.fields] == ["Scale"]
    assert message.struct_value.fields[0].value.float_value == 2.0


def test_struct_values_round_trip_with_nesting(classes):
    import tempo_sim.unreal as unreal
    import fixture_classes.structs as structs

    original = structs.FWidgetSettings(
        Scale=1.5, Inner=structs.FWidgetTrim(Offset=unreal.FVector(1.0, 2.0, 3.0)))
    back = unreal._from_value(unreal._to_value("struct_value", original))
    # The generated value type, not a bare one: it is what the stub promised a read returns.
    assert type(back) is structs.FWidgetSettings
    assert type(back.Inner) is structs.FWidgetTrim
    assert back == original


def test_an_array_of_struct_values_round_trips(classes):
    import tempo_sim.unreal as unreal
    import fixture_classes.structs as structs

    history = [structs.FWidgetSettings(Scale=1.0), structs.FWidgetSettings(Scale=2.0)]
    assert unreal._from_value(unreal._to_value("struct_array_value", history)) == history


def test_a_read_value_carries_its_read_only_members(classes):
    """The server reports every member. A scenario cannot set Label, but it can read one back."""
    import tempo_sim.unreal as unreal
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    message = WorldControl.Value()
    message.struct_value.struct_name = "FWidgetSettings"
    field = message.struct_value.fields.add()
    field.name = "Label"
    field.value.string_value = "from the sim"
    assert unreal._from_value(message).Label == "from the sim"


@pytest.mark.parametrize("case,build", [
    ("an unknown member", lambda s: s.FWidgetSettings(Scal=1.0)),
    ("a read-only member", lambda s: s.FWidgetSettings(Label="x")),
    ("a positional argument", lambda s: s.FWidgetSettings(1.0)),
])
def test_a_struct_value_is_checked_as_it_is_built(classes, case, build):
    import fixture_classes.structs as structs
    with pytest.raises((AttributeError, TypeError)):
        build(structs)


def test_a_member_that_was_not_given_says_so(classes):
    import fixture_classes.structs as structs
    with pytest.raises(AttributeError, match="not set"):
        _ = structs.FWidgetSettings(Scale=1.0).Inner


def test_a_struct_the_package_does_not_declare_still_reads(classes):
    import tempo_sim.unreal as unreal
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    message = WorldControl.Value()
    message.struct_value.struct_name = "FFromANewerSim"
    field = message.struct_value.fields.add()
    field.name = "Count"
    field.value.int_value = 3
    value = unreal._from_value(message)
    assert isinstance(value, unreal.Struct)
    assert value.Count == 3


def test_a_function_takes_and_returns_a_struct_value(classes, fake_calls):
    import fixture_classes.structs as structs
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    returned = WorldControl.Value()
    returned.struct_value.struct_name = "FWidgetSettings"
    field = returned.struct_value.fields.add()
    field.name = "Scale"
    field.value.float_value = 4.0
    fake_calls["results"] = [WorldControl.FunctionResult(name="ReturnValue", typed_value=returned)]

    widget = classes.Widget("MyActor")
    result = widget.Configure(structs.FWidgetSettings(Scale=2.0))
    argument = fake_calls["calls"][0]["args"][0]
    assert argument["name"] == "NewSettings"
    assert argument["value"].struct_value.fields[0].value.float_value == 2.0
    assert result == structs.FWidgetSettings(Scale=4.0)


@pytest.mark.parametrize("name", ["GetLimits", "ApplyLimits"])
def test_a_function_whose_struct_cannot_travel_is_not_offered(classes, fake_calls, name):
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError):
        getattr(widget, name)


def test_a_proxy_passed_as_a_value_is_read_first(classes, monkeypatch):
    """`f(truck.Settings)` passes what the property holds, not a handle the server cannot take."""
    import tempo_sim.unreal as unreal
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

    read = WorldControl.Value()
    read.struct_value.struct_name = "FWidgetSettings"
    field = read.struct_value.fields.add()
    field.name = "Scale"
    field.value.float_value = 9.0
    asked = []

    class FakeWorld:
        @staticmethod
        def get_property(**kwargs):
            asked.append(kwargs["property"])
            return read

    monkeypatch.setattr(unreal, "_world", lambda: FakeWorld)
    widget = classes.Widget("MyActor")
    message = unreal._to_value("struct_value", widget.Settings)
    assert asked == ["Settings"]
    assert message.struct_value.fields[0].value.float_value == 9.0
    assert unreal.value_of(widget.Settings).Scale == 9.0



# -- two installed packages -----------------------------------------------------------------------
#
# A project installs its own classes; it may also have the published ones that ship inside
# tempo_sim (TYPED_WORLD_API_PLAN.md section 16). They describe different things, so the registry
# holds both -- and an inheritance chain crosses from one to the other, because a project's classes
# derive from engine classes the published package declares.

@pytest.fixture(scope="module")
def two_packages(tmp_path_factory):
    """One package holding `Object`, another holding what derives from it.

    Split by Unreal package, which is how the real split falls: the published set is chosen by
    module, so a project keeps whatever is outside it and inherits across the boundary.
    """
    import json

    with open(FIXTURE) as f:
        schema = json.load(f)

    def part(packages, name, out):
        subset = dict(schema)
        subset["classes"] = [cls for cls in schema["classes"] if cls["package"] in packages]
        document = out / (name + ".json")
        document.write_text(json.dumps(subset))
        subprocess.run([sys.executable, GENERATOR, str(document), str(out / name)],
                       check=True, capture_output=True, text=True)

    out = tmp_path_factory.mktemp("split")
    part({"/Script/CoreUObject"}, "published_classes", out)
    part({"/Script/Engine", "/Script/SomePlugin"}, "project_classes", out)

    installed = _tempo_sim_modules()
    for name in installed:
        del sys.modules[name]
    sys.path.insert(0, str(out))
    sys.path.insert(0, RUNTIME_API)
    try:
        import published_classes  # noqa: F401  (importing is what registers it)
        import project_classes  # noqa: F401
        import project_classes.Engine as engine
        import tempo_sim.unreal as runtime
        assert runtime.__file__.startswith(RUNTIME_API)
        yield engine, runtime
    finally:
        sys.path.remove(str(out))
        sys.path.remove(RUNTIME_API)
        for name in _tempo_sim_modules():
            del sys.modules[name]
        sys.modules.update(installed)


def test_both_packages_are_registered(two_packages):
    """Python's register() used to overwrite, so the first import became unresolvable -- silently,
    since its own generated classes go on working and only the metadata is gone."""
    _, runtime = two_packages
    registered = {package.package for package in runtime.registered_classes().packages}
    assert registered == {"published_classes", "project_classes"}


def test_a_class_resolves_from_whichever_package_has_it(two_packages):
    _, runtime = two_packages
    classes = runtime.registered_classes()
    assert classes.module_of("/Script/CoreUObject.Object") == "CoreUObject"
    assert classes.module_of(WIDGET_PATH) == "Engine"


def test_an_inherited_property_is_found_across_the_boundary(two_packages, wire):
    """Widget is in one package and the Object it derives from is in the other.

    Each class records only what it declares, so reading an inherited property means walking
    `super` -- and that walk has to cross packages or the published classes buy nothing.
    """
    engine, _ = two_packages
    widget = engine.Widget("MyActor")
    assert widget.Name == "read-back", "Object.Name should resolve from the other package"
    assert wire["reads"] == [("MyActor", "", "Name")]


def test_is_a_crosses_the_boundary(two_packages):
    engine, runtime = two_packages
    classes = runtime.registered_classes()
    assert classes.is_subclass(WIDGET_PATH, "/Script/CoreUObject.Object")


def test_the_dynamic_path_reaches_either_package(two_packages):
    _, runtime = two_packages
    classes = runtime.registered_classes()
    assert classes.python_class(WIDGET_PATH).__name__ == "Widget"
    assert classes.python_class("/Script/CoreUObject.Object").__name__ == "Object"

