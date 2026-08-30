# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for the TempoWorld actor/state/property API against a packaged sim.

These exercise the spawn -> query -> move -> destroy lifecycle and, along the way, the
meters/right-handed <-> Unreal-native coordinate conversion at the API boundary.
"""

import math
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


def test_call_function_returns_scalar_result(sim_server):
    """A function's return value comes back stringified, using the same type vocabulary and
    format GetProperties uses. AActor::GetDistanceTo returns a float in Unreal units."""
    a = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 70.0)).name
    b = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(5.0, 0.0, 70.0)).name
    try:
        resp = (tw.call(actor=a, function="GetDistanceTo")
                .actor_arg("OtherActor", b)
                .execute())
        assert len(resp.results) == 1
        result = resp.results[0]
        assert result.name == "ReturnValue"
        assert result.property_type == "float"
        # Floats are not unit-converted, matching set_float_property: 5 m is 500 cm.
        assert float(result.value) == pytest.approx(500.0, abs=1.0)
    finally:
        for name in (a, b):
            tw.destroy_actor(actor=name)


def test_call_function_returns_struct_result(sim_server):
    """A struct return is stringified exactly as GetProperties renders the same struct."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(1.0, 2.0, 75.0)).name
    try:
        resp = tw.call(actor=name, function="K2_GetActorLocation").execute()
        assert len(resp.results) == 1
        assert resp.results[0].property_type == "vector"
        x, y, z = (float(v) for v in re.findall(r"-?\d+\.?\d*", resp.results[0].value))
        # Unreal-native: metres -> centimetres, and Y negated (left-handed).
        assert (x, y, z) == pytest.approx((100.0, -200.0, 7500.0), abs=1.0)
    finally:
        tw.destroy_actor(actor=name)


def test_call_function_void_returns_no_results(sim_server):
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 80.0)).name
    try:
        resp = tw.call(actor=name, function="SetActorHiddenInGame").bool_arg("bNewHidden", True).execute()
        assert list(resp.results) == []
    finally:
        tw.destroy_actor(actor=name)


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


def _vector(x, y, z):
    return Geometry.Vector(x=x, y=y, z=z)


def _rotation(r, p, y):
    return Geometry.Rotation(r=r, p=p, y=y)


def _floats(text):
    return tuple(float(v) for v in re.findall(r"-?\d+\.?\d*", text))


def _location_of(name, component):
    return _floats(_component_property(name, component, "RelativeLocation"))


def _scale_of(name, component=None):
    return _floats(_component_property(name, component or _root_component(name), "RelativeScale3D"))


def _child_component(name):
    """A non-root scene component, since the component RPCs refuse to retarget the root."""
    root = _root_component(name)
    return tw.add_component(
        component_type="StaticMeshComponent", actor=name, parent=root, transform=_transform(0.0, 0.0, 0.0)
    ).name


def test_set_actor_transform_preserves_scale(sim_server):
    """TempoCore.Transform has no scale field, so a set_actor_transform request expresses a pose
    and nothing more. Moving an actor must not resize it."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 70.0)).name
    try:
        _make_movable(name)
        tw.call(actor=name, function="SetActorScale3D").vector_arg(
            "NewScale3D", x=2.0, y=3.0, z=4.0
        ).execute()

        tw.set_actor_transform(actor=name, transform=_transform(7.0, 0.0, 70.0))

        moved = tw.get_current_actor_state(actor=name)
        assert moved.transform.location.x == pytest.approx(7.0, abs=0.05)

        scale = _component_property(name, _root_component(name), "RelativeScale3D")
        x, y, z = (float(v) for v in re.findall(r"-?\d+\.?\d*", scale))
        assert (x, y, z) == pytest.approx((2.0, 3.0, 4.0), abs=1e-3)
    finally:
        tw.destroy_actor(actor=name)


def test_set_actor_transform_relative_to_actor_composes_like_spawn(sim_server):
    """`relative_to_actor` means "in that actor's frame", so the requested transform is applied
    first and the reference actor's transform second - the same order spawn_actor composes in.

    The reference actor is yawed 90 degrees so the two possible orders are distinguishable: with
    the offset in the reference frame the child lands at (10, 1), and with the operands swapped it
    would land at (11, 0).
    """
    ref_transform = _transform(10.0, 0.0, 80.0)
    ref_transform.rotation.y = math.pi / 2.0
    ref = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=ref_transform).name
    child = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 80.0)).name
    try:
        _make_movable(child)
        tw.set_actor_transform(
            actor=child, transform=_transform(1.0, 0.0, 0.0), relative_to_actor=ref
        )

        state = tw.get_current_actor_state(actor=child)
        assert state.transform.location.x == pytest.approx(10.0, abs=0.05)
        assert state.transform.location.y == pytest.approx(1.0, abs=0.05)
    finally:
        for name in (child, ref):
            tw.destroy_actor(actor=name)


def test_set_actor_transform_only_sets_what_was_supplied(sim_server):
    """Partial update: a member the request leaves unset keeps the value it already had."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(1.0, 2.0, 85.0)).name
    try:
        _make_movable(name)
        tw.set_actor_rotation(actor=name, rotation=_rotation(0.0, 0.0, math.pi / 2.0))
        tw.set_actor_scale3_d(actor=name, scale=_vector(2.0, 2.0, 2.0))

        # Location only: rotation and scale must survive.
        location_only = Geometry.Transform()
        location_only.location.x = 9.0
        location_only.location.y = 2.0
        location_only.location.z = 85.0
        tw.set_actor_transform(actor=name, transform=location_only)

        state = tw.get_current_actor_state(actor=name)
        assert state.transform.location.x == pytest.approx(9.0, abs=0.05)
        assert state.transform.rotation.y == pytest.approx(math.pi / 2.0, abs=1e-3)
        assert _scale_of(name) == pytest.approx((2.0, 2.0, 2.0), abs=1e-3)

        # Rotation only: location and scale must survive.
        rotation_only = Geometry.Transform()
        rotation_only.rotation.y = 0.0
        tw.set_actor_transform(actor=name, transform=rotation_only)

        state = tw.get_current_actor_state(actor=name)
        assert state.transform.location.x == pytest.approx(9.0, abs=0.05)
        assert state.transform.rotation.y == pytest.approx(0.0, abs=1e-3)
        assert _scale_of(name) == pytest.approx((2.0, 2.0, 2.0), abs=1e-3)

        # An empty transform with only a scale touches nothing but the scale.
        tw.set_actor_transform(actor=name, transform=Geometry.Transform(), scale=_vector(3.0, 3.0, 3.0))
        state = tw.get_current_actor_state(actor=name)
        assert state.transform.location.x == pytest.approx(9.0, abs=0.05)
        assert _scale_of(name) == pytest.approx((3.0, 3.0, 3.0), abs=1e-3)
    finally:
        tw.destroy_actor(actor=name)


def test_set_actor_location_rotation_scale(sim_server):
    """The three singular actor RPCs each move exactly one part of the transform."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 90.0)).name
    try:
        _make_movable(name)

        tw.set_actor_location(actor=name, location=_vector(4.0, -3.0, 90.0))
        state = tw.get_current_actor_state(actor=name)
        assert state.transform.location.x == pytest.approx(4.0, abs=0.05)
        # Negative Y round-trips, so location goes through the handedness conversion.
        assert state.transform.location.y == pytest.approx(-3.0, abs=0.05)

        tw.set_actor_rotation(actor=name, rotation=_rotation(0.0, 0.0, math.pi / 2.0))
        state = tw.get_current_actor_state(actor=name)
        assert state.transform.rotation.y == pytest.approx(math.pi / 2.0, abs=1e-3)
        assert state.transform.location.x == pytest.approx(4.0, abs=0.05), "rotating moved the actor"

        # Scale is unitless and unconverted: a negative Y would mirror, not re-express.
        tw.set_actor_scale3_d(actor=name, scale=_vector(2.0, 3.0, 4.0))
        assert _scale_of(name) == pytest.approx((2.0, 3.0, 4.0), abs=1e-3)
        state = tw.get_current_actor_state(actor=name)
        assert state.transform.location.x == pytest.approx(4.0, abs=0.05), "scaling moved the actor"
    finally:
        tw.destroy_actor(actor=name)


def test_set_actor_location_respects_relative_to_actor(sim_server):
    """The singular location RPC reads `relative_to_actor` the same way set_actor_transform does."""
    ref_transform = _transform(10.0, 0.0, 95.0)
    ref_transform.rotation.y = math.pi / 2.0
    ref = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=ref_transform).name
    child = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 95.0)).name
    try:
        _make_movable(child)
        tw.set_actor_location(actor=child, location=_vector(1.0, 0.0, 0.0), relative_to_actor=ref)

        state = tw.get_current_actor_state(actor=child)
        assert state.transform.location.x == pytest.approx(10.0, abs=0.05)
        assert state.transform.location.y == pytest.approx(1.0, abs=0.05)
    finally:
        for name in (child, ref):
            tw.destroy_actor(actor=name)


def test_component_location_rotation_scale(sim_server):
    """The component RPCs mirror the actor ones, in the space `relative_to_world` selects."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 100.0)).name
    try:
        _make_movable(name)
        child = _child_component(name)

        tw.set_component_scale3_d(actor=name, component=child, scale=_vector(2.0, 3.0, 4.0))
        assert _scale_of(name, child) == pytest.approx((2.0, 3.0, 4.0), abs=1e-3)

        tw.set_component_location(actor=name, component=child, location=_vector(1.0, 0.0, 0.0))
        assert _location_of(name, child)[0] == pytest.approx(100.0, abs=0.05)  # 1 m -> 100 cm
        assert _scale_of(name, child) == pytest.approx((2.0, 3.0, 4.0), abs=1e-3), "moving rescaled it"

        tw.set_component_rotation(actor=name, component=child, rotation=_rotation(0.0, 0.0, math.pi / 2.0))
        assert _scale_of(name, child) == pytest.approx((2.0, 3.0, 4.0), abs=1e-3), "turning rescaled it"
    finally:
        tw.destroy_actor(actor=name)


def test_set_component_transform_partial_and_scale(sim_server):
    """SetComponentTransform takes the same partial update and scale field as the actor RPC."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 105.0)).name
    try:
        _make_movable(name)
        child = _child_component(name)

        tw.set_component_transform(
            actor=name, component=child, transform=Geometry.Transform(), scale=_vector(2.0, 3.0, 4.0)
        )
        assert _scale_of(name, child) == pytest.approx((2.0, 3.0, 4.0), abs=1e-3)

        location_only = Geometry.Transform()
        location_only.location.x = 1.0
        tw.set_component_transform(actor=name, component=child, transform=location_only)
        assert _scale_of(name, child) == pytest.approx((2.0, 3.0, 4.0), abs=1e-3)
        assert _location_of(name, child)[0] == pytest.approx(100.0, abs=0.05)
    finally:
        tw.destroy_actor(actor=name)


def test_singular_transform_rpcs_require_their_value(sim_server):
    """An unset value on a singular RPC is a mistake, not a request to zero the field -
    silently teleporting to the origin or collapsing to zero scale would be worse."""
    name = tw.spawn_actor(actor_type=SPAWNABLE_TYPE, transform=_transform(0.0, 0.0, 110.0)).name
    try:
        _make_movable(name)
        for call in (
            lambda: tw.set_actor_location(actor=name),
            lambda: tw.set_actor_rotation(actor=name),
            lambda: tw.set_actor_scale3_d(actor=name),
        ):
            with pytest.raises(grpc.RpcError) as excinfo:
                call()
            assert excinfo.value.code() == grpc.StatusCode.FAILED_PRECONDITION

        state = tw.get_current_actor_state(actor=name)
        assert state.transform.location.z == pytest.approx(110.0, abs=0.05), "a rejected call still moved the actor"
    finally:
        tw.destroy_actor(actor=name)
