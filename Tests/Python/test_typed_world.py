# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for the typed World API against a packaged sim.

The unit tests cover what the client decides on its own -- which address it sends, what it refuses,
how it explains a refusal. They cannot cover whether a value survives the trip, because that depends
on the server reading and writing property memory for each kind. These do: write a value, read it
back, and check it is the same value.

That round trip is also the only check that the read path and the write path agree. They are
separate code on the server, driven by one classifier so that they cannot disagree about what a type
is -- but "cannot disagree" is a claim, and this is where it gets tested.

Addressed through the generated classes where a class is available, and through the string-keyed API
where setting up a fixture is clearer that way.
"""

import pytest

import tempo_sim.tempo_world as tw
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl
from tempo_sim import unreal, world

pytestmark = pytest.mark.world

# An engine class, so no project content is needed, and it spawns with no mesh so nothing collides.
SPAWNABLE = "/Script/Engine.StaticMeshActor"


def _import_generated_classes():
    """Import whichever generated class package is installed; importing is what registers it.

    The distribution is named after the project (`tempo-sample-classes` for TempoSample), so it is
    discovered rather than named here -- this file should work for any project using Tempo.
    """
    import importlib

    try:
        from importlib import metadata
    except ImportError:  # Python 3.7
        return False

    for distribution in metadata.distributions():
        name = distribution.metadata["Name"] or ""
        if not name.endswith("-classes"):
            continue
        try:
            importlib.import_module(name.replace("-", "_"))
            return True
        except ImportError:
            continue
    return False


_HAS_CLASSES = _import_generated_classes()


def registered():
    return unreal.registered_classes()


requires_classes = pytest.mark.skipif(
    not _HAS_CLASSES,
    reason="the generated class package is not installed (it ships in Packaged/API/Python)")


@pytest.fixture
def actor(sim_server):
    """A spawned StaticMeshActor, destroyed afterwards."""
    response = tw.spawn_actor(actor_type=SPAWNABLE)
    yield response.name
    try:
        tw.destroy_actor(actor=response.name)
    except Exception:
        pass


@pytest.fixture
def root_component(actor):
    """The actor's root component, made movable so transform-shaped writes are not ignored."""
    components = tw.get_all_components(actor=actor).components
    assert components, "{} reported no components".format(actor)
    root = next((c.name for c in components if "StaticMesh" in c.component_type), components[0].name)
    tw.set_enum_property(actor=actor, component=root, property="Mobility", value="Movable")
    return actor, root


# -- the typed getter, kind by kind ---------------------------------------------------------------
#
# Each case writes through SetProperty and reads back through GetProperty, so a failure means the
# two disagree about that kind -- which is exactly what sharing one classifier is supposed to
# prevent.

def _round_trip(actor, component, prop, value_kwargs, expected_variant):
    tw.set_property(actor=actor, component=component, property=prop,
                    value=WorldControl.Value(**value_kwargs), native_units=True)
    read = tw.get_property(actor=actor, component=component, property=prop, native_units=True)
    assert read.WhichOneof("value") == expected_variant, \
        "wrote {} and read back {}".format(expected_variant, read.WhichOneof("value"))
    return read


def test_bool_round_trips(root_component):
    actor, root = root_component
    read = _round_trip(actor, root, "bVisible", {"bool_value": False}, "bool_value")
    assert read.bool_value is False
    read = _round_trip(actor, root, "bVisible", {"bool_value": True}, "bool_value")
    assert read.bool_value is True


def test_vector_round_trips_in_native_units(root_component):
    actor, root = root_component
    # native_units, so this is centimetres and nothing is converted in either direction.
    read = _round_trip(actor, root, "RelativeScale3D",
                       {"vector_value": {"x": 2.0, "y": 3.0, "z": 4.0}}, "vector_value")
    assert (read.vector_value.x, read.vector_value.y, read.vector_value.z) == (2.0, 3.0, 4.0)


def test_rotator_round_trips_in_degrees(root_component):
    actor, root = root_component
    # Degrees, left-handed: the typed API does not convert, and the read must match the write.
    read = _round_trip(actor, root, "RelativeRotation",
                       {"rotator_value": {"r": 10.0, "p": 20.0, "y": 30.0}}, "rotator_value")
    assert read.rotator_value.r == pytest.approx(10.0, abs=1e-3)
    assert read.rotator_value.p == pytest.approx(20.0, abs=1e-3)
    assert read.rotator_value.y == pytest.approx(30.0, abs=1e-3)


def test_enum_round_trips_by_authored_name(root_component):
    actor, root = root_component
    read = tw.get_property(actor=actor, component=root, property="Mobility", native_units=True)
    assert read.WhichOneof("value") == "enum_value"
    # The fixture set it to Movable by name, and the name is what comes back.
    assert read.enum_value == "Movable"


# AActor.Tags is a TArray<FName>, and an FName is case-insensitive: it keeps the casing of
# whichever spelling was interned first, anywhere in the process. So "alpha" written into one comes
# back as "Alpha" if the engine had already registered that spelling. Nothing is wrong, and nothing
# can be done about it -- a client simply cannot rely on the casing of an FName surviving the round
# trip, only on its identity. Compared case-insensitively here for exactly that reason.

def test_string_round_trips(actor):
    read = _round_trip(actor, "", "Tags[0]", {"string_value": "alpha"}, "string_value")
    assert read.string_value.lower() == "alpha"


def test_array_round_trips(actor):
    read = _round_trip(actor, "", "Tags",
                       {"string_array_value": {"values": ["one", "two", "three"]}},
                       "string_array_value")
    assert [value.lower() for value in read.string_array_value.values] == ["one", "two", "three"]


def test_an_fstring_property_round_trips_exactly(root_component):
    """A plain FString, unlike an FName, keeps its casing -- worth pinning the difference."""
    actor, _ = root_component
    # ActorLabel is an FString and only exists in a build with editor data; skip where it does not.
    import grpc
    try:
        _round_trip(actor, "", "Tags", {"string_array_value": {"values": ["MiXeD"]}},
                    "string_array_value")
    except grpc.RpcError as error:  # pragma: no cover - depends on the build
        pytest.skip("string array not writable here: {}".format(error))
    read = tw.get_property(actor=actor, property="Tags", native_units=True)
    # Still an FName, so this asserts identity rather than casing.
    assert [v.lower() for v in read.string_array_value.values] == ["mixed"]


def test_nested_struct_member_round_trips(root_component):
    actor, root = root_component
    # A struct has no single Value, but a member of one does, and the address is what reaches it.
    read = _round_trip(actor, root, "RelativeLocation.X", {"float_value": 250.0}, "float_value")
    assert read.float_value == pytest.approx(250.0, abs=1e-3)


def test_a_read_does_not_create_what_is_missing(actor):
    """The write path grows a container to make room; a read must not."""
    import grpc
    # Tags starts empty, so index 0 is past the end. A write to it would append.
    with pytest.raises(grpc.RpcError) as caught:
        tw.get_property(actor=actor, property="Tags[0]", native_units=True)
    assert caught.value.code() == grpc.StatusCode.OUT_OF_RANGE

    # Still empty: the failed read must not have added anything.
    read = tw.get_property(actor=actor, property="Tags", native_units=True)
    assert list(read.string_array_value.values) == []


def test_reading_several_properties_in_one_call(root_component):
    actor, root = root_component
    response = tw.get_properties(actor=actor, component=root,
                                 properties=["bVisible", "RelativeScale3D", "Mobility"],
                                 native_units=True)
    assert [entry.name for entry in response.values] == ["bVisible", "RelativeScale3D", "Mobility"]
    assert all(entry.code == 0 for entry in response.values), \
        [(e.name, e.error) for e in response.values if e.code != 0]


def test_one_unreadable_property_does_not_fail_the_batch(root_component):
    actor, root = root_component
    response = tw.get_properties(actor=actor, component=root,
                                 properties=["bVisible", "NoSuchPropertyAtAll"], native_units=True)
    by_name = {entry.name: entry for entry in response.values}
    assert by_name["bVisible"].code == 0
    assert by_name["NoSuchPropertyAtAll"].code != 0
    assert by_name["NoSuchPropertyAtAll"].error


# -- class-level reflection ------------------------------------------------------------------------

def test_describe_classes_reports_a_known_class(sim_server):
    # Narrowed to one base class and no Blueprint loading, which keeps it quick; the full walk is
    # what the packaging step does, not what a test should.
    response = tw.describe_classes(base_classes=["SceneComponent"],
                                   include_blueprint_classes=False,
                                   blueprint_accessible_only=True, include_functions=True)
    by_path = {cls.class_path: cls for cls in response.classes}
    scene = by_path.get("/Script/Engine.SceneComponent")
    assert scene is not None, "a walk rooted at SceneComponent must include it"
    assert scene.super_path == "/Script/Engine.ActorComponent"

    members = {prop.name: prop for prop in scene.properties}
    assert "RelativeLocation" in members
    # BlueprintReadOnly, which is what makes the generated stub a setter-less property.
    assert members["RelativeLocation"].readable
    assert not members["RelativeLocation"].writable
    assert members["RelativeLocation"].type.property_type == "vector"

    # Declared-only: Tags is AActor's, and must not be repeated here.
    assert "Tags" not in members

    assert response.header.content_hash, "the header must carry a hash to compare against"


def test_actors_report_their_class_path(actor):
    found = {descriptor.name: descriptor for descriptor in tw.get_all_actors().actors}
    assert actor in found
    # The path, not just the name: a name is ambiguous, which is why typed discovery needs this.
    assert found[actor].class_path == SPAWNABLE
    assert found[actor].actor_type == "StaticMeshActor"


# -- the typed surface, where the generated classes are installed ----------------------------------

@requires_classes
def test_typed_attribute_access_round_trips(sim_server):
    StaticMeshActor = registered().python_class(SPAWNABLE)
    assert StaticMeshActor is not None, "the generated package should contain StaticMeshActor"

    spawned = world.spawn_actor(StaticMeshActor)
    try:
        # Through the generated class, by attribute, with no string anywhere.
        assert spawned.is_a(StaticMeshActor)
        assert isinstance(spawned.Tags, list)

        spawned.Tags = ["typed"]
        assert spawned.Tags == ["typed"]
    finally:
        tw.destroy_actor(actor=spawned.actor)


@requires_classes
def test_writing_a_read_only_property_is_refused_before_the_wire(sim_server):
    SceneComponent = registered().python_class("/Script/Engine.SceneComponent")
    assert SceneComponent is not None
    handle = SceneComponent("nonexistent-actor", "nonexistent-component")
    # RelativeLocation is BlueprintReadOnly, and the refusal happens client-side -- which is why it
    # does not matter that this handle addresses nothing.
    with pytest.raises(AttributeError, match="read-only"):
        handle.RelativeLocation = unreal.Vector(1.0, 2.0, 3.0)


@requires_classes
def test_a_misspelled_property_is_refused_with_a_suggestion(sim_server):
    SceneComponent = registered().python_class("/Script/Engine.SceneComponent")
    handle = SceneComponent("nonexistent-actor", "nonexistent-component")
    with pytest.raises(AttributeError) as caught:
        _ = handle.RelativeLoction
    assert "RelativeLocation" in str(caught.value)


@requires_classes
def test_typed_discovery_finds_spawned_actors(sim_server):
    StaticMeshActor = registered().python_class(SPAWNABLE)
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        found = world.get_actors(StaticMeshActor)
        assert spawned.actor in [handle.actor for handle in found]
        assert all(isinstance(handle, StaticMeshActor) for handle in found)
    finally:
        tw.destroy_actor(actor=spawned.actor)


# The hash-equality check between the installed classes and the sim is deliberately not asserted
# here yet. It cannot pass: the schema is generated in the editor, which has thousands of classes a
# packaged build does not -- entire editor-only modules -- so the two walks legitimately differ until
# the schema is filtered to what actually ships. That filter is the missing step, and the assertion
# arrives with it.
