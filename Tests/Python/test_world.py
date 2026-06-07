# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for the TempoWorld actor/state/property API against a packaged sim.

These exercise the spawn -> query -> move -> destroy lifecycle and, along the way, the
meters/right-handed <-> Unreal-native coordinate conversion at the API boundary.
"""

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
