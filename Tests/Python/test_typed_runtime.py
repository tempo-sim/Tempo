# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the runtime that serves the generated typed classes.

Attribute access is the API, so these check what an access actually does: which address it sends,
what it refuses, and how it explains a refusal. No sim: the two functions that reach the wire are
replaced, and the rest is real -- the generated package, the metadata, the Value conversion.

    pytest -m codegen Tests/Python
"""

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


@pytest.fixture(scope="module")
def classes(tmp_path_factory):
    """The fixture schema generated and importable, with the runtime registered against it."""
    out = tmp_path_factory.mktemp("runtime")
    subprocess.run([sys.executable, GENERATOR, FIXTURE, str(out / "fixture_classes")],
                   check=True, capture_output=True, text=True)
    sys.path.insert(0, str(out))
    sys.path.insert(0, RUNTIME_API)
    try:
        import fixture_classes  # noqa: F401  (importing is what registers it)
        import fixture_classes.Engine as engine
        yield engine
    finally:
        sys.path.remove(str(out))
        sys.path.remove(RUNTIME_API)


@pytest.fixture
def wire(monkeypatch):
    """Replace the two functions that reach the server, and record what they were asked to do."""
    import tempo_sim.unreal as unreal

    calls = {"reads": [], "writes": []}

    def fake_read(actor, component, address):
        calls["reads"].append((actor, component, address))
        return "read-back"

    def fake_write(actor, component, address, member, value):
        calls["writes"].append((actor, component, address, value))

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


def test_struct_cannot_be_assigned_whole(classes, wire):
    widget = classes.Widget("MyActor")
    with pytest.raises(AttributeError, match="members"):
        widget.Settings = None
    assert wire["writes"] == []


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

    for original in (unreal.Vector(1.0, 2.0, 3.0),
                     unreal.Vector2D(4.0, 5.0),
                     unreal.IntVector(1, 2, 3),
                     unreal.IntPoint(6, 7),
                     unreal.Rotator(10.0, 20.0, 30.0),
                     unreal.Quat(0.0, 0.0, 0.0, 1.0),
                     unreal.Color(1, 2, 3)):
        variant = {
            "Vector": "vector_value", "Vector2D": "vector2d_value", "IntVector": "int_vector_value",
            "IntPoint": "int_point_value", "Rotator": "rotator_value", "Quat": "quat_value",
            "Color": "color_value",
        }[type(original).__name__]
        assert unreal._from_value(unreal._to_value(variant, original)) == original

    pose = unreal.Transform(unreal.Vector(1.0, 2.0, 3.0), unreal.Rotator(4.0, 5.0, 6.0))
    assert unreal._from_value(unreal._to_value("transform_value", pose)) == pose


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
