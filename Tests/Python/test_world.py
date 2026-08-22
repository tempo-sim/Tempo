# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for the TempoWorld actor/state/property API against a packaged sim.

These exercise the spawn -> query -> move -> destroy lifecycle and, along the way, the
meters/right-handed <-> Unreal-native coordinate conversion at the API boundary.
"""

import grpc
import pytest

import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

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
