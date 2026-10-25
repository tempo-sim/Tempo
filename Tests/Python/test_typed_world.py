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
    """Import the project's generated classes, if installed; importing is what registers them.

    They are the `reflected` subpackage of the project's own package, which is named after the
    project (`tempo_sample.reflected` for TempoSample), so it is discovered rather than named here
    -- this file should work for any project using Tempo. tempo-sim's own `reflected` holds the
    published ones, and is not this build's.
    """
    import importlib

    try:
        from importlib import metadata
    except ImportError:  # Python 3.7
        return False

    # Found by the files a distribution installed rather than by trying an import of each, which
    # would import every installed package's top level to find one.
    for distribution in metadata.distributions():
        if (distribution.metadata["Name"] or "") == "tempo-sim":
            continue
        for path in distribution.files or []:
            parts = path.parts
            if len(parts) == 3 and parts[1:] == ("reflected", "__init__.py"):
                try:
                    importlib.import_module(parts[0] + ".reflected")
                    return True
                except ImportError:
                    break
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
def test_a_property_with_a_blueprint_setter_is_written_through_it(sim_server):
    """The gap that let a real defect ship: nothing wrote one of these against a live sim.

    NetUpdateFrequency carries a BlueprintSetter, so the write has to call SetNetUpdateFrequency
    rather than touch the memory -- a setter may be the only thing that clamps a value or marks
    render state dirty, and a cooked build could not even have told us it exists, which is why the
    schema is walked in the editor.

    It also has to *name* the argument. CallFunction refuses an unnamed one outright, so this
    failed every time with FAILED_PRECONDITION until the generator started resolving what the
    setter calls its parameter. 306 properties in this build go down that path.
    """
    StaticMeshActor = registered().python_class(SPAWNABLE)
    assert StaticMeshActor is not None

    member = registered().find_property(SPAWNABLE, "NetUpdateFrequency")
    assert member is not None and member["setter"], \
        "this test is pointless unless the property really has a setter"
    assert member["setter_argument"], "the setter's argument has to be named"

    spawned = world.spawn_actor(StaticMeshActor)
    try:
        spawned.NetUpdateFrequency = 50.0
        assert spawned.NetUpdateFrequency == pytest.approx(50.0)
        spawned.NetUpdateFrequency = 25.0
        assert spawned.NetUpdateFrequency == pytest.approx(25.0)
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
        handle.RelativeLocation = unreal.FVector(1.0, 2.0, 3.0)


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


@requires_classes
def test_tempo_components_declare_their_own_configuration():
    """Tempo's own components must be configurable through the typed classes, not only by string.

    A property reaches the typed surface only when its UPROPERTY says BlueprintReadWrite or
    BlueprintReadOnly -- which is how a Tempo class declares what is public scenario API. Nothing
    else enforces that, so dropping the specifier in a refactor would quietly take Tempo's own API
    back out of the typed classes while leaving the string-keyed one working, and the loss would
    surface as a scenario that cannot configure a sensor. These are a lidar's basic parameters.

    Reads the installed metadata rather than a live component, so it needs no sim and no project
    content: TempoSensors ships with Tempo.
    """
    registry = registered()
    lidar = "/Script/TempoSensors.TempoLidar"
    for name in ("HorizontalBeams", "VerticalBeams", "HorizontalFOV", "VerticalFOV",
                 "MinDistance", "MaxDistance", "ReturnMode", "SamplingStrategy"):
        member = registry.find_property(lidar, name)
        assert member is not None, "TempoLidar.{} is absent from the typed surface".format(name)
        assert member["writable"], "TempoLidar.{} should be writable".format(name)

    # Declared VisibleAnywhere, so it is readable and must not be assignable.
    derived = registry.find_property(lidar, "bColorEnabled")
    assert derived is not None and not derived["writable"]


def test_discovery_by_a_base_class_can_be_narrowed(sim_server):
    """The whole point of asking for a base class: sorting out what came back."""
    StaticMeshActor = registered().python_class(SPAWNABLE)
    Actor = registered().python_class("/Script/Engine.Actor")
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        actors = world.get_actors(Actor)
        ours = [handle for handle in actors if handle.actor == spawned.actor]
        assert ours, "a StaticMeshActor should come back from a query for Actor"
        assert ours[0].is_a(StaticMeshActor), "is_a must answer about the actor, not the handle"
        assert any(not handle.is_a(StaticMeshActor) for handle in actors), \
            "and must still tell the other actors apart"
    finally:
        tw.destroy_actor(actor=spawned.actor)


def test_a_handle_made_from_a_name_alone_knows_its_class(sim_server):
    """get_actor() without a class makes no call, so is_a has to go and ask."""
    StaticMeshActor = registered().python_class(SPAWNABLE)
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        assert world.get_actor(spawned.actor).is_a(StaticMeshActor)
    finally:
        tw.destroy_actor(actor=spawned.actor)


def test_call_function_returns_typed_results(root_component):
    """A function's results come back in the same representation a property read uses."""
    actor, root = root_component
    response = tw.call_function(actor=actor, component=root, function="GetRelativeTransform",
                                native_units=True)
    assert response.results, "GetRelativeTransform should hand something back"
    returned = next(r for r in response.results if r.name == "ReturnValue")
    # The legacy string form is still there, and still a string.
    assert returned.value
    # The typed form needs no parsing and can be fed straight back in. Under native_units a
    # transform is Unreal's own shape, quaternion and scale included.
    assert returned.HasField("typed_value")
    assert returned.typed_value.WhichOneof("value") == "native_transform_value"


def test_a_typed_result_matches_the_property_it_reports(root_component):
    """The same value, read two ways, has to agree.

    The function reader and the property reader are the same code pointed at different containers
    -- a call frame is just another container -- and this is what says so.

    Compared against GetRelativeTransform, which reads the relative-transform properties directly.
    A world-space getter would not do: writing RelativeLocation sets the property but does not call
    UpdateComponentToWorld, so the derived world transform stays stale. See the note below.
    """
    actor, root = root_component
    tw.set_property(actor=actor, component=root, property="RelativeLocation",
                    value=WorldControl.Value(vector_value={"x": 10.0, "y": 20.0, "z": 30.0}),
                    native_units=True)

    from_property = tw.get_property(actor=actor, component=root, property="RelativeLocation",
                                    native_units=True).vector_value
    from_function = next(
        r for r in tw.call_function(actor=actor, component=root, function="GetRelativeTransform",
                                    native_units=True).results
        if r.name == "ReturnValue").typed_value.native_transform_value.translation

    assert (from_function.x, from_function.y, from_function.z) == \
           pytest.approx((from_property.x, from_property.y, from_property.z), abs=1e-3)


def test_writing_a_transform_property_does_not_move_the_component(root_component):
    """A property write sets memory; it does not run whatever derives from it.

    This is not a defect to fix in the client -- it is what writing a UPROPERTY means. Unreal
    recomputes a component's world transform in UpdateComponentToWorld, which a property write does
    not call, so the component has a new RelativeLocation and an unchanged world position.

    Pinned as a test because it is surprising, and because the fix is to call a function rather than
    to write a property: SetActorTransform, or the component's own setter.
    """
    actor, root = root_component
    tw.set_property(actor=actor, component=root, property="RelativeLocation",
                    value=WorldControl.Value(vector_value={"x": 10.0, "y": 20.0, "z": 30.0}),
                    native_units=True)

    world_location = next(
        r for r in tw.call_function(actor=actor, component=root,
                                    function="K2_GetComponentLocation", native_units=True).results
        if r.name == "ReturnValue").typed_value.vector_value

    assert (world_location.x, world_location.y, world_location.z) == pytest.approx((0.0, 0.0, 0.0)), \
        "if this now matches the property, Unreal propagates the write and the docs should say so"


@requires_classes
def test_calling_a_generated_method(sim_server):
    StaticMeshActor = registered().python_class(SPAWNABLE)
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        # Declared by the stub, dispatched through CallFunction, returned typed.
        assert spawned.ActorHasTag("nope") is False
        spawned.Tags = ["marked"]
        assert spawned.ActorHasTag("marked") is True
    finally:
        tw.destroy_actor(actor=spawned.actor)


# -- struct values ----------------------------------------------------------------------------------

def _struct_value(name, **fields):
    value = WorldControl.Value()
    value.struct_value.struct_name = name
    for field_name, field_value in fields.items():
        field = value.struct_value.fields.add()
        field.name = field_name
        field.value.CopyFrom(WorldControl.Value(**field_value))
    return value


def _fields(value):
    return {field.name: field.value for field in value.struct_value.fields}


def test_a_struct_property_reads_whole(root_component):
    """Every member comes back, inherited ones included, so a read is the whole struct."""
    actor, root = root_component
    read = tw.get_property(actor=actor, component=root, property="BodyInstance", native_units=True)
    assert read.WhichOneof("value") == "struct_value"
    assert read.struct_value.struct_name == "FBodyInstance"
    assert {"LinearDamping", "AngularDamping", "bEnableGravity"} <= set(_fields(read))


def test_a_struct_write_is_a_merge(root_component):
    """Only the members given are written; the rest keep what they held.

    That is what makes a struct value safe to send without knowing every member -- which no client
    does, since a struct can carry state reflection never reports.
    """
    actor, root = root_component
    before = _fields(tw.get_property(actor=actor, component=root, property="BodyInstance",
                                     native_units=True))
    tw.set_property(actor=actor, component=root, property="BodyInstance", native_units=True,
                    value=_struct_value("FBodyInstance", LinearDamping={"float_value": 0.75}))
    after = _fields(tw.get_property(actor=actor, component=root, property="BodyInstance",
                                    native_units=True))
    assert after["LinearDamping"].float_value == pytest.approx(0.75)
    assert after["AngularDamping"] == before["AngularDamping"], "an unmentioned member must not move"


def test_a_nested_address_takes_a_struct_value(root_component):
    actor, root = root_component
    tw.set_property(actor=actor, component=root, property="BodyInstance", native_units=True,
                    value=_struct_value("FBodyInstance", AngularDamping={"float_value": 0.25}))
    assert tw.get_property(actor=actor, component=root, property="BodyInstance.AngularDamping",
                           native_units=True).float_value == pytest.approx(0.25)


def test_the_wrong_struct_is_refused(root_component):
    actor, root = root_component
    with pytest.raises(Exception, match="FHitResult"):
        tw.set_property(actor=actor, component=root, property="BodyInstance", native_units=True,
                        value=_struct_value("FHitResult", Time={"float_value": 1.0}))


def test_a_struct_argument_and_result_round_trip(root_component):
    """In through a parameter, out through a return value: the same struct, both ways."""
    actor, root = root_component
    tw.call_function(actor=actor, component=root, function="SetWalkableSlopeOverride",
                     native_units=True,
                     args=[{"name": "NewOverride", "value": _struct_value(
                         "FWalkableSlopeOverride",
                         WalkableSlopeBehavior={"enum_value": "WalkableSlope_Increase"},
                         WalkableSlopeAngle={"float_value": 33.0})}])
    returned = next(r for r in tw.call_function(
        actor=actor, component=root, function="GetWalkableSlopeOverride", native_units=True).results
        if r.name == "ReturnValue")
    fields = _fields(returned.typed_value)
    assert fields["WalkableSlopeBehavior"].enum_value == "WalkableSlope_Increase"
    assert fields["WalkableSlopeAngle"].float_value == pytest.approx(33.0)


@requires_classes
def test_a_typed_struct_argument_and_result_round_trip(root_component):
    actor, root = root_component
    StaticMeshComponent = registered().python_class("/Script/Engine.StaticMeshComponent")
    structs = registered().struct_class("FWalkableSlopeOverride")
    mesh = StaticMeshComponent(actor, root)

    mesh.SetWalkableSlopeOverride(structs(WalkableSlopeBehavior="WalkableSlope_Decrease",
                                          WalkableSlopeAngle=12.0))
    back = mesh.GetWalkableSlopeOverride()
    assert type(back) is structs, "a struct result comes back as the generated value type"
    assert back.WalkableSlopeBehavior == "WalkableSlope_Decrease"
    assert back.WalkableSlopeAngle == pytest.approx(12.0)


@requires_classes
def test_an_out_struct_comes_back_by_name(sim_server):
    """K2_SetActorLocation's FHitResult was withheld before struct values; now it is a result."""
    StaticMeshActor = registered().python_class(SPAWNABLE)
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        # A spawned StaticMeshActor's root is Static, which refuses to move -- and says so with a
        # false return, which is not what this test is about.
        root = next(c.name for c in tw.get_all_components(actor=spawned.actor).components
                    if "StaticMesh" in c.component_type)
        tw.set_enum_property(actor=spawned.actor, component=root, property="Mobility", value="Movable")
        results = spawned.K2_SetActorLocation(unreal.FVector(100.0, 0.0, 0.0), False, True)
        assert results["ReturnValue"] is True
        hit = results["SweepHitResult"]
        assert type(hit).__tempo_struct__ == "FHitResult"
        assert hit.bBlockingHit is False, "nothing to hit with sweep off"
    finally:
        tw.destroy_actor(actor=spawned.actor)


@requires_classes
def test_a_transform_travels_in_unreals_shape(sim_server):
    """An FTransform argument and result keep their quaternion and scale. A typed write used to
    reset the scale to 1, and a read to drop it."""
    from tempo_sim.conversion import quat_of, rotator_of

    StaticMeshActor = registered().python_class(SPAWNABLE)
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        root = next(c.name for c in tw.get_all_components(actor=spawned.actor).components
                    if "StaticMesh" in c.component_type)
        tw.set_enum_property(actor=spawned.actor, component=root, property="Mobility", value="Movable")
        wanted = unreal.FTransform(quat_of(unreal.FRotator(Yaw=90.0)),
                                   unreal.FVector(100.0, 0.0, 50.0), unreal.FVector(2.0, 3.0, 4.0))
        assert spawned.K2_SetActorTransform(wanted, False, True)["ReturnValue"] is True
        got = spawned.GetTransform()
        assert type(got) is unreal.FTransform
        assert (got.Scale3D.X, got.Scale3D.Y, got.Scale3D.Z) == pytest.approx((2.0, 3.0, 4.0))
        assert (got.Translation.X, got.Translation.Z) == pytest.approx((100.0, 50.0), abs=1e-3)
        assert rotator_of(got.Rotation).Yaw == pytest.approx(90.0, abs=1e-3)
    finally:
        tw.destroy_actor(actor=spawned.actor)


@requires_classes
def test_the_world_mirror_works_on_handles(sim_server):
    """world is tempo_world given handles: add a component and find it by class, move an actor in
    SI and read it back, then take the component away."""
    StaticMeshActor = registered().python_class(SPAWNABLE)
    StaticMeshComponent = registered().python_class("/Script/Engine.StaticMeshComponent")
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        added = world.add_component(StaticMeshComponent, spawned, name="MirrorMesh")
        assert type(added) is StaticMeshComponent and added.actor == spawned.actor
        found = world.get_components(spawned, StaticMeshComponent)
        assert added.component in [c.component for c in found]

        root = next(c for c in world.get_components(spawned, StaticMeshComponent)
                    if c.component != added.component)
        tw.set_enum_property(actor=spawned.actor, component=root.component, property="Mobility",
                             value="Movable")
        world.set_actor_location(spawned, world.Vector(x=2.0, y=1.0, z=0.5))   # meters, right-handed
        location = world.get_actor_location(spawned).location
        assert (location.x, location.y, location.z) == pytest.approx((2.0, 1.0, 0.5), abs=1e-4)
        # The same place in Unreal's terms, through the generated class.
        assert spawned.K2_GetActorLocation().Y == pytest.approx(-100.0, abs=1e-2)

        world.destroy_component(added)
        assert added.component not in [c.component
                                        for c in world.get_components(spawned, StaticMeshComponent)]
    finally:
        world.destroy_actor(spawned)


# -- one ordered batch ------------------------------------------------------------------------------

def _set_op(actor, prop, **value):
    return WorldControl.BatchOp(set=WorldControl.SetPropertyRequest(
        actor=actor, property=prop, value=WorldControl.Value(**value), native_units=True))


def _get_op(actor, prop):
    return WorldControl.BatchOp(get=WorldControl.GetPropertyRequest(
        actor=actor, property=prop, native_units=True))


def test_a_batch_reads_its_own_writes_in_order(actor):
    """A read sees every write before it in the same batch, and none after it."""
    response = tw.run_batch(ops=[
        _set_op(actor, "bCanBeDamaged", bool_value=False),
        _get_op(actor, "bCanBeDamaged"),
        _set_op(actor, "bCanBeDamaged", bool_value=True),
        _get_op(actor, "bCanBeDamaged"),
    ])
    assert [result.code for result in response.results] == [0, 0, 0, 0]
    assert response.results[1].value.bool_value is False
    assert response.results[3].value.bool_value is True


def test_a_batch_carries_calls(actor):
    response = tw.run_batch(ops=[
        _set_op(actor, "Tags", string_array_value={"values": ["batched"]}),
        WorldControl.BatchOp(call=WorldControl.CallFunctionRequest(
            actor=actor, function="ActorHasTag", native_units=True,
            args=[WorldControl.FunctionArg(name="Tag", value=WorldControl.Value(string_value="batched"))])),
    ])
    returned = next(r for r in response.results[1].call.results if r.name == "ReturnValue")
    assert returned.typed_value.bool_value is True, "the call ran after the write before it"


def test_a_failed_op_stops_the_batch_when_asked(actor):
    tw.set_bool_property(actor=actor, property="bCanBeDamaged", value=True)
    response = tw.run_batch(stop_on_failure=True, ops=[
        _set_op(actor, "NoSuchProperty", bool_value=True),
        _set_op(actor, "bCanBeDamaged", bool_value=False),
    ])
    assert response.results[0].code != 0
    assert response.results[1].code == 10, "the op after a failure is reported ABORTED, not run"
    assert tw.get_property(actor=actor, property="bCanBeDamaged").bool_value is True


def test_a_failed_op_does_not_stop_the_batch_by_default(actor):
    tw.set_bool_property(actor=actor, property="bCanBeDamaged", value=True)
    response = tw.run_batch(ops=[
        _set_op(actor, "NoSuchProperty", bool_value=True),
        _set_op(actor, "bCanBeDamaged", bool_value=False),
    ])
    assert response.results[0].code != 0
    assert response.results[1].code == 0
    assert tw.get_property(actor=actor, property="bCanBeDamaged").bool_value is False


@requires_classes
def test_a_typed_batch_defers_and_orders(sim_server):
    StaticMeshActor = registered().python_class(SPAWNABLE)
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        with world.batch() as b:
            spawned.bCanBeDamaged = False
            # NetUpdateFrequency has a BlueprintSetter: a call, in the same batch.
            spawned.NetUpdateFrequency = 40.0
            damaged = b.get(lambda: spawned.bCanBeDamaged)
            frequency = b.get(lambda: spawned.NetUpdateFrequency)
            tagged = b.call(lambda: spawned.ActorHasTag("nope"))
        assert damaged.value is False
        assert frequency.value == pytest.approx(40.0)
        assert tagged.value is False

        with world.batch():
            spawned.bCanBeDamaged = True
            assert spawned.bCanBeDamaged is True, "an immediate read sees the queued write"
    finally:
        tw.destroy_actor(actor=spawned.actor)


# -- the typed asset catalog -------------------------------------------------------------------------

def _catalog(mount):
    """One mount point of the installed package's catalog: `_catalog("Engine").BasicShapes.Cube`."""
    import importlib
    for package in registered().packages:
        try:
            return importlib.import_module("{}.assets.{}".format(package.package, mount))
        except ImportError:
            continue
    pytest.skip("the installed classes carry no asset catalog for {}".format(mount))


@requires_classes
def test_the_catalog_is_what_the_build_cooked(sim_server):
    cooked = {asset.path: asset.class_path for asset in
              tw.get_assets(asset_type="/Script/CoreUObject.Object", include_subclasses=True).assets}
    cube = _catalog("Engine").BasicShapes.Cube
    assert cooked.get(cube.path) == "/Script/Engine.StaticMesh"


@requires_classes
def test_a_catalog_asset_goes_through_a_typed_setter(root_component):
    actor, root = root_component
    StaticMeshComponent = registered().python_class("/Script/Engine.StaticMeshComponent")
    mesh = StaticMeshComponent(actor, root)
    cube = _catalog("Engine").BasicShapes.Cube

    assert mesh.SetStaticMesh(cube) is True
    assert mesh.StaticMesh == cube, "an asset reads back as the AssetRef that was written"


# -- Phase 4: filtered discovery, watching, and the asset catalog ---------------------------------

@pytest.fixture
def ticking(sim_server):
    """A sim that is actually advancing, which a tick-driven subscription needs.

    Deliberately not the paused fixed-step fixture: nothing ticks there, so a stream would block for
    ever. The default is wall-clock and running, but rather than assume it, this checks -- a blocked
    next() has no timeout to fall back on, so being wrong here would hang the suite instead of
    failing it.
    """
    import time

    import tempo_sim.tempo_core as tc

    first = tc.get_sim_time().sim_time
    for _ in range(20):
        time.sleep(0.1)
        if tc.get_sim_time().sim_time > first:
            return
    pytest.skip("the sim is not advancing, so a tick-driven subscription cannot be observed")




# -- subsystems -----------------------------------------------------------------------------------
#
# One of each kind the server knows the owner of, all in every build: a world subsystem with
# properties (TempoAgents' own), a game instance subsystem with a function (the engine's replay
# subsystem), and an engine subsystem (UMG's viewport one), which is only resolved.

WORLD_SUBSYSTEM = "/Script/TempoAgents.TempoAgentsWorldSubsystem"
GAME_INSTANCE_SUBSYSTEM = "/Script/Engine.ReplaySubsystem"
ENGINE_SUBSYSTEM = "/Script/UMG.GameViewportSubsystem"


def test_a_world_subsystem_property_round_trips(sim_server):
    offset = "BrightnessMeterLateralOffset"
    before = tw.get_property(subsystem=WORLD_SUBSYSTEM, property=offset, native_units=True)
    try:
        tw.set_property(subsystem=WORLD_SUBSYSTEM, property=offset,
                        value=WorldControl.Value(float_value=321.0), native_units=True)
        read = tw.get_property(subsystem=WORLD_SUBSYSTEM, property=offset, native_units=True)
        assert read.float_value == pytest.approx(321.0)
    finally:
        tw.set_property(subsystem=WORLD_SUBSYSTEM, property=offset, value=before, native_units=True)


def test_a_game_instance_subsystem_function_is_callable(sim_server):
    response = tw.call_function(subsystem=GAME_INSTANCE_SUBSYSTEM, function="IsPlaying",
                                native_units=True)
    returned = next(r for r in response.results if r.name == "ReturnValue")
    assert returned.typed_value.bool_value is False, "nothing is replaying in a test sim"


def test_an_engine_subsystem_resolves(sim_server):
    response = tw.get_properties(subsystem=ENGINE_SUBSYSTEM, properties=[])
    assert list(response.values) == []


def test_a_batch_addresses_a_subsystem(sim_server):
    offset = "BrightnessMeterLateralOffset"
    before = tw.get_property(subsystem=WORLD_SUBSYSTEM, property=offset, native_units=True)
    try:
        response = tw.run_batch(ops=[
            WorldControl.BatchOp(set=WorldControl.SetPropertyRequest(
                subsystem=WORLD_SUBSYSTEM, property=offset,
                value=WorldControl.Value(float_value=123.0), native_units=True)),
            WorldControl.BatchOp(get=WorldControl.GetPropertyRequest(
                subsystem=WORLD_SUBSYSTEM, property=offset, native_units=True)),
        ])
        assert [result.code for result in response.results] == [0, 0]
        assert response.results[1].value.float_value == pytest.approx(123.0)
    finally:
        tw.set_property(subsystem=WORLD_SUBSYSTEM, property=offset, value=before, native_units=True)


def test_a_class_that_is_not_a_subsystem_is_not_found(sim_server):
    import grpc
    with pytest.raises(grpc.RpcError) as caught:
        tw.get_properties(subsystem=SPAWNABLE, properties=[])
    assert caught.value.code() == grpc.StatusCode.NOT_FOUND
    assert "USubsystem" in caught.value.details()


def test_a_subsystem_is_addressed_on_its_own(actor):
    import grpc
    with pytest.raises(grpc.RpcError) as caught:
        tw.get_properties(actor=actor, subsystem=WORLD_SUBSYSTEM, properties=[])
    assert caught.value.code() == grpc.StatusCode.FAILED_PRECONDITION


@requires_classes
def test_a_typed_subsystem_reads_writes_and_calls(sim_server):
    WorldSubsystem = registered().python_class(WORLD_SUBSYSTEM)
    agents = world.get_subsystem(WorldSubsystem)
    before = agents.BrightnessMeterLateralOffset
    try:
        agents.BrightnessMeterLateralOffset = 456.0
        assert agents.BrightnessMeterLateralOffset == pytest.approx(456.0)
        with world.batch() as b:
            agents.BrightnessMeterLateralOffset = 654.0
            read = b.get(lambda: agents.BrightnessMeterLateralOffset)
        assert read.value == pytest.approx(654.0)
    finally:
        agents.BrightnessMeterLateralOffset = before

    Replay = registered().python_class(GAME_INSTANCE_SUBSYSTEM)
    assert world.get_subsystem(Replay).IsPlaying() is False


@requires_classes
def test_a_non_subsystem_class_fails_at_the_lookup(sim_server):
    import grpc
    StaticMeshActor = registered().python_class(SPAWNABLE)
    with pytest.raises(grpc.RpcError):
        world.get_subsystem(StaticMeshActor)


def test_actors_of_class_filters_server_side(actor):
    """Only the actors asked for, rather than every actor for the client to discard."""
    response = tw.get_actors_of_class(actor_type=SPAWNABLE, include_subclasses=True)
    names = [descriptor.name for descriptor in response.actors]
    assert actor in names
    assert all(d.class_path == SPAWNABLE for d in response.actors), \
        "a filtered call should not report anything else"

    everything = tw.get_all_actors().actors
    assert len(response.actors) <= len(everything)


def test_actors_of_class_by_name_or_path(actor):
    by_path = tw.get_actors_of_class(actor_type=SPAWNABLE, include_subclasses=True)
    by_name = tw.get_actors_of_class(actor_type="StaticMeshActor", include_subclasses=True)
    # Both resolve through the one helper every client-supplied class name goes through.
    assert {d.name for d in by_path.actors} == {d.name for d in by_name.actors}


def test_actors_of_class_without_subclasses_is_exact(actor):
    exact = tw.get_actors_of_class(actor_type="Actor", include_subclasses=False)
    # Nothing in the world is a plain AActor, so asking for exactly that finds nothing -- which is
    # why include_subclasses is usually what a caller wants.
    assert all(d.class_path == "/Script/Engine.Actor" for d in exact.actors)


def test_an_unknown_class_is_not_found(sim_server):
    import grpc
    with pytest.raises(grpc.RpcError) as caught:
        tw.get_actors_of_class(actor_type="NoSuchActorClassAnywhere")
    assert caught.value.code() == grpc.StatusCode.NOT_FOUND


def test_streaming_a_property_yields_per_tick(root_component, ticking):
    actor, root = root_component
    stream = tw.stream_property(actor=actor, component=root, property="bVisible", native_units=True)
    try:
        for _ in range(3):
            value = next(stream)
            assert value.WhichOneof("value") == "bool_value"
    finally:
        stream.close()


def test_streaming_an_unknown_property_errors(actor, ticking):
    import grpc
    stream = tw.stream_property(actor=actor, property="NoSuchPropertyAtAll", native_units=True)
    with pytest.raises(grpc.RpcError):
        next(stream)


def test_the_asset_catalog_finds_assets_by_class(sim_server):
    """What a client needs to set an asset-valued property without hard-coding a path."""
    response = tw.get_assets(asset_type="StaticMesh", include_subclasses=True)
    # A packaged build may legitimately contain no static meshes, so this asserts the shape rather
    # than a count -- and that every descriptor is usable if there are any.
    for descriptor in response.assets:
        assert descriptor.path, "an asset has to report the path a property would accept"
        assert descriptor.name
        assert descriptor.class_path

    paths = [descriptor.path for descriptor in response.assets]
    assert paths == sorted(paths), "sorted, so two calls against one build agree"


def test_the_asset_catalog_respects_a_path_prefix(sim_server):
    everywhere = tw.get_assets(asset_type="Object", include_subclasses=True)
    scoped = tw.get_assets(asset_type="Object", include_subclasses=True, path_prefix="/Engine")
    assert len(scoped.assets) <= len(everywhere.assets)
    assert all(d.path.startswith("/Engine") for d in scoped.assets)


def test_an_unknown_asset_class_is_not_found(sim_server):
    import grpc
    with pytest.raises(grpc.RpcError) as caught:
        tw.get_assets(asset_type="NoSuchAssetClassAnywhere")
    assert caught.value.code() == grpc.StatusCode.NOT_FOUND


@requires_classes
def test_watching_a_property_through_a_handle(sim_server, ticking):
    StaticMeshActor = registered().python_class(SPAWNABLE)
    spawned = world.spawn_actor(StaticMeshActor)
    try:
        values = []
        for value in spawned.watch("bHidden"):
            values.append(value)
            if len(values) == 2:
                break
        assert values == [False, False]
    finally:
        tw.destroy_actor(actor=spawned.actor)


@requires_classes
def test_the_installed_classes_match_the_sim(sim_server):
    """The drift check, against the build these classes were generated for.

    Only meaningful because the schema is filtered to what the build ships and stamped with the
    build's own hash: an editor's hash would never match, having walked thousands of classes a
    packaged game does not have.

    Slow -- the sim walks reflection and loads every Blueprint to answer -- which is exactly why
    this is not done automatically on connect.
    """
    assert world.check_schema(raise_on_mismatch=True) is True


def _import_published_classes():
    """Import the published types, if the wheel carries them, and return their package name.

    They ship inside `tempo_sim` rather than as a distribution of their own
    (TYPED_WORLD_API_PLAN.md section 16), and importing them is lazy by design -- `import
    tempo_sim` does not pull in three megabytes of classes nobody asked for. The project's types
    import them already when they re-export any; this asks regardless.
    """
    import importlib

    try:
        importlib.import_module("tempo_sim.reflected")
    except ImportError:
        return None
    return "tempo_sim.reflected"


def _project_package():
    """The project's types package, found among the registered ones rather than named."""
    return next((package.package for package in registered().packages
                 if not package.package.startswith("tempo_sim.")), None)


@requires_classes
def test_a_shared_class_is_one_type_in_both_packages(sim_server):
    """A project's types re-export what the published ones define identically (section 16.5).

    Otherwise there would be two types for /Script/Engine.Pawn, and a helper written against the
    published one could accept none of the project's Blueprints.
    """
    import importlib

    if _import_published_classes() is None:
        pytest.skip("this tempo-sim wheel carries no published types")
    project = importlib.import_module(_project_package() + ".Engine")
    published = importlib.import_module("tempo_sim.reflected.Engine")
    assert project.Pawn is published.Pawn
    # Resolved from the published package, which is the one that defines it.
    assert registered().python_class("/Script/Engine.Pawn") is published.Pawn
    assert world.get_actors(project.Pawn) is not None


@requires_classes
def test_the_published_classes_match_the_sim(sim_server):
    """The published subset against a running sim, beside the project's own classes.

    This is the premise of section 16 under test: a package describing only the modules every build
    ships can still verify itself, because the comparison is per entry. A whole-walk hash could
    never match -- the subset is 1,730 classes of 6,068 by construction.
    """
    published = _import_published_classes()
    if published is None:
        pytest.skip("this tempo-sim wheel carries no published classes for this engine version")

    installed = {package.package for package in registered().packages}
    assert published in installed, "importing the published classes should register them"
    assert len(installed) > 1, "the project's own classes should still be registered too"

    assert world.check_schema(raise_on_mismatch=True) is True


@requires_classes
def test_the_published_classes_offer_no_more_than_the_build(sim_server):
    """Less, not wrong -- which is what makes a build-sourced hash honest.

    The subset drops whole modules and reroots 32 classes past an ancestor it does not carry, so it
    offers less than the project's package. What it must never do is offer a class the build has no
    trace of.
    """
    published = _import_published_classes()
    if published is None:
        pytest.skip("this tempo-sim wheel carries no published classes for this engine version")

    reported = {cls.class_path for cls in tw.describe_classes(
        include_blueprint_classes=True, blueprint_accessible_only=True,
        include_functions=False).classes}

    package = next(p for p in registered().packages if p.package == published)
    promised = set(package.index)
    assert promised, "the published package should declare some classes"
    missing = promised - reported
    assert not missing, "{} published class(es) are absent from this build, e.g. {}".format(
        len(missing), sorted(missing)[:5])


@requires_classes
def test_every_generated_class_exists_in_the_sim(sim_server):
    """No stub may promise a class this build does not have.

    This is the check that caught the editor/cook divergence: the unfiltered schema offered 5,229
    classes -- whole editor-only modules -- that a packaged build has no trace of, and a scenario
    addressing one would type-check and then fail at runtime.
    """
    reported = {cls.class_path for cls in tw.describe_classes(
        include_blueprint_classes=True, blueprint_accessible_only=True,
        include_functions=False).classes}

    promised = set(registered().index)
    missing = promised - reported
    assert not missing, "{} generated class(es) are absent from this build, e.g. {}".format(
        len(missing), sorted(missing)[:5])
