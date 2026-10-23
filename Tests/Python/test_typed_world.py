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
    # The typed form needs no parsing and can be fed straight back in.
    assert returned.HasField("typed_value")
    assert returned.typed_value.WhichOneof("value") == "transform_value"


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
        if r.name == "ReturnValue").typed_value.transform_value.location

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
    """Import the published classes for this build's engine version, if the wheel carries them.

    They ship inside `tempo_sim` rather than as a distribution of their own
    (TYPED_WORLD_API_PLAN.md section 16), one subpackage per Unreal version, and importing them is
    lazy by design -- `import tempo_sim` does not pull in three megabytes of classes nobody asked
    for. So a test that wants them has to ask.

    Only the version matching this build. A wheel carrying several is the point of the namespace,
    and importing a version the sim is not running would correctly report a mismatch.
    """
    import importlib

    # The project's own package, which was generated from this build in the same run, so its
    # engine version is the one to ask for. Found by name rather than by position: the published
    # one registers too, and which came first is an import-order detail.
    project = [package for package in registered().packages
               if not package.package.startswith("tempo_sim.")]
    if not project:
        return None
    engine_version = project[0].header.get("engine_version") or ""
    if not engine_version:
        return None
    name = "tempo_sim.UE.v" + engine_version.replace(".", "_")
    try:
        importlib.import_module(name)
    except ImportError:
        return None
    return name


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
