# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for the TempoWorld actor/state/property API against a packaged sim.

These exercise the spawn -> query -> move -> destroy lifecycle and, along the way, the
meters/right-handed <-> Unreal-native coordinate conversion at the API boundary.
"""

import re

import grpc
import pytest

import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

pytestmark = pytest.mark.world

# StaticMeshActor is an engine class (always available, no project content needed) and spawns
# with no mesh, so it has no collision to perturb the requested spawn location.
SPAWNABLE_TYPE = "StaticMeshActor"


def _transform(x, y, z):
    t = Geometry.Transform()
    t.location.x = x
    t.location.y = y
    t.location.z = z
    return t


def _make_movable(name):
    """A StaticMeshActor spawns with Static mobility, which Unreal refuses to move at runtime
    (SetActorTransform is a no-op). Flip its root component to Movable first — this also exercises
    set_enum_property (the enum byte is resolved by authored name, e.g. "Movable")."""
    comps = tw.get_all_components(actor=name).components
    assert comps, f"{name} reported no components"
    root = next((c.name for c in comps if "StaticMesh" in c.component_type), comps[0].name)
    tw.set_enum_property(actor=name, component=root, property="Mobility", value="Movable")


def test_get_all_actors_returns_descriptors(sim_server):
    resp = tw.get_all_actors()
    # Don't assume the map is non-empty; just assert the response shape and descriptor fields.
    for a in resp.actors:
        assert a.name
        assert a.actor_type


def test_spawn_query_move_destroy(sim_server):
    spawned = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(1.0, 2.0, 3.0))
    name = spawned.name
    assert name, "spawn_actor returned an empty actor name"

    try:
        # Spawn location should round-trip through the meters/right-handed conversion.
        state = tw.get_current_actor_state(actor=name)
        assert state.transform.location.x == pytest.approx(1.0, abs=0.05)
        assert state.transform.location.y == pytest.approx(2.0, abs=0.05)
        assert state.transform.location.z == pytest.approx(3.0, abs=0.05)

        # Moving the actor is reflected in its reported state (after making it movable).
        _make_movable(name)
        tw.set_actor_transform(actor=name, transform=_transform(11.0, 2.0, 3.0))
        moved = tw.get_current_actor_state(actor=name)
        assert moved.transform.location.x == pytest.approx(11.0, abs=0.05)
        assert moved.transform.location.y == pytest.approx(2.0, abs=0.05)
    finally:
        tw.destroy_actor(actor=name)

    remaining = [a.name for a in tw.get_all_actors().actors]
    assert name not in remaining, "destroyed actor still present in get_all_actors"


def test_spawn_accepts_either_blueprint_spelling(sim_server):
    """A Blueprint asset reads as "BP_Foo" in the editor while the class it generates is "BP_Foo_C",
    so SpawnActor accepts either, preferring an exact match on the real class name.

    Exercised with a native class: "StaticMeshActor" is the exact match, and "StaticMeshActor_C"
    only resolves via the de-suffixing fallback.
    """
    names = []
    try:
        for actor_type in (SPAWNABLE_TYPE, f"{SPAWNABLE_TYPE}_C"):
            spawned = tw.spawn_actor(actor_type=actor_type, transform=_transform(0.0, 0.0, 20.0))
            assert spawned.name, f"spawn_actor({actor_type!r}) returned an empty actor name"
            names.append(spawned.name)

        resolved = {a.actor_type for a in tw.get_all_actors().actors if a.name in names}
        assert resolved == {SPAWNABLE_TYPE}, f"both spellings should resolve to {SPAWNABLE_TYPE}, got {resolved}"
    finally:
        for name in names:
            tw.destroy_actor(actor=name)


def test_unknown_actor_type_is_not_found(sim_server):
    # Leniency about the "_C" suffix must not turn a genuinely bad type name into a match.
    with pytest.raises(grpc.RpcError) as excinfo:
        tw.spawn_actor(actor_type="NotARealActorClass_C", transform=_transform(0.0, 0.0, 0.0))
    assert excinfo.value.code() == grpc.StatusCode.NOT_FOUND


def test_reported_actor_type_round_trips_into_spawn(sim_server):
    """Reported class names are the real UClass names, so a client can feed a descriptor's
    actor_type straight back into spawn_actor."""
    first = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 20.0))
    names = [first.name]
    try:
        reported = next(a.actor_type for a in tw.get_all_actors().actors if a.name == first.name)
        again = tw.spawn_actor(actor_type=reported, transform=_transform(0.0, 0.0, 25.0))
        assert again.name, f"spawn_actor({reported!r}) returned an empty actor name"
        names.append(again.name)
    finally:
        for name in names:
            tw.destroy_actor(actor=name)


def test_negative_y_round_trips_handedness(sim_server):
    # The API is right-handed; internally Unreal is left-handed (Y negated). A round trip
    # through spawn + query should preserve a negative Y.
    spawned = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, -5.0, 10.0))
    name = spawned.name
    try:
        state = tw.get_current_actor_state(actor=name)
        assert state.transform.location.y == pytest.approx(-5.0, abs=0.05)
    finally:
        tw.destroy_actor(actor=name)


def _root_component(name):
    comps = tw.get_all_components(actor=name).components
    assert comps, f"{name} reported no components"
    return next((c.name for c in comps if "StaticMesh" in c.component_type), comps[0].name)


def _actor_property(name, prop):
    props = tw.get_actor_properties(actor=name).properties
    return next(p.value for p in props if p.name == prop)


def _component_property(name, component, prop):
    props = tw.get_component_properties(actor=name, component=component).properties
    return next(p.value for p in props if p.name == prop)


def test_call_function_with_bool_arg(sim_server):
    """CallFunction fills a UFUNCTION's parameter frame from typed args. AActor's
    SetActorHiddenInGame writes bHidden, which the property getter can read back."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 30.0)).name
    try:
        assert _actor_property(name, "bHidden") == "false"
        tw.call(actor=name, function="SetActorHiddenInGame").bool_arg("bNewHidden", True).execute()
        assert _actor_property(name, "bHidden") == "true"
    finally:
        tw.destroy_actor(actor=name)


def test_call_function_with_multiple_args(sim_server):
    """USceneComponent::SetVisibility takes two bools. Its second parameter has a C++ default,
    but defaults live in editor-only metadata, so the API requires every input parameter."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 35.0)).name
    try:
        root = _root_component(name)
        assert _component_property(name, root, "bVisible") == "true"
        (tw.call(actor=name, component=root, function="SetVisibility")
            .bool_arg("bNewVisibility", False)
            .bool_arg("bPropagateToChildren", False)
            .execute())
        assert _component_property(name, root, "bVisible") == "false"
    finally:
        tw.destroy_actor(actor=name)


def test_call_function_with_struct_arg(sim_server):
    """A struct parameter goes through the same conversion rules as the matching
    set_*_property RPC — vector_arg is unitless, exactly like set_vector_property."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 40.0)).name
    try:
        _make_movable(name)
        (tw.call(actor=name, function="SetActorScale3D")
            .vector_arg("NewScale3D", x=2.0, y=3.0, z=4.0)
            .execute())

        scale = _component_property(name, _root_component(name), "RelativeScale3D")
        x, y, z = (float(v) for v in re.findall(r"-?\d+\.?\d*", scale))
        assert (x, y, z) == pytest.approx((2.0, 3.0, 4.0), abs=1e-3)
    finally:
        tw.destroy_actor(actor=name)


def test_call_function_with_nested_arg_path(sim_server):
    """Argument names accept the same nested addressing as property names."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 45.0)).name
    try:
        _make_movable(name)
        (tw.call(actor=name, function="SetActorScale3D")
            .float_arg("NewScale3D.X", 5.0)
            .execute())

        scale = _component_property(name, _root_component(name), "RelativeScale3D")
        x = float(re.findall(r"-?\d+\.?\d*", scale)[0])
        assert x == pytest.approx(5.0, abs=1e-3)
    finally:
        tw.destroy_actor(actor=name)


def test_call_function_missing_arg_is_rejected(sim_server):
    """A function with parameters can't be invoked with none supplied — the frame would be
    filled with zeroes the caller never asked for."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 50.0)).name
    try:
        with pytest.raises(grpc.RpcError) as excinfo:
            tw.call_function(actor=name, function="SetActorHiddenInGame")
        assert excinfo.value.code() == grpc.StatusCode.FAILED_PRECONDITION
        assert "bNewHidden" in excinfo.value.details()
    finally:
        tw.destroy_actor(actor=name)


def test_call_function_unknown_arg_is_not_found(sim_server):
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 55.0)).name
    try:
        with pytest.raises(grpc.RpcError) as excinfo:
            (tw.call(actor=name, function="SetActorHiddenInGame")
                .bool_arg("bNewHidden", True)
                .bool_arg("NotAParameter", True)
                .execute())
        assert excinfo.value.code() == grpc.StatusCode.NOT_FOUND
    finally:
        tw.destroy_actor(actor=name)


def test_call_function_with_no_args_still_works(sim_server):
    """The pre-existing zero-argument form must keep working untouched."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 60.0)).name
    try:
        tw.call_function(actor=name, function="K2_DestroyActor")
    except Exception:
        tw.destroy_actor(actor=name)
        raise
    assert not any(a.name == name for a in tw.get_all_actors().actors)


def test_set_property_generic_matches_typed(sim_server):
    """The generic SetProperty carries the value's type in the value, and must behave
    identically to the singular set_*_property RPC it generalizes."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 65.0)).name
    try:
        root = _root_component(name)
        tw.set_property(
            actor=name,
            component=root,
            property="Mobility",
            value=WorldControl.Value(enum_value="Movable"),
        )
        assert _component_property(name, root, "Mobility") == "Movable"

        tw.set_property(
            actor=name,
            property="bHidden",
            value=WorldControl.Value(bool_value=True),
        )
        assert _actor_property(name, "bHidden") == "true"
    finally:
        tw.destroy_actor(actor=name)
