// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Integration tests for the generated `tempo-sim` Rust client against a packaged sim.
//!
//! Scripts/TestRustAPI.sh launches the packaged sim (headless) before running these and tears it
//! down after. They mirror the Python `core`/`world` groups. Run via
//! Scripts/TestRustAPI.sh integration. Run single-threaded (the script passes --test-threads=1)
//! since they share the client's global connection context and drive the same sim.

use std::time::{Duration, Instant};

use tempo_sim::proto::tempo_core::{Rotation, Transform, Vector};
use tempo_sim::proto::tempo_world::value::Value as V;
use tempo_sim::proto::tempo_world::Value;
use tempo_sim::tempo_world;

/// Point the client at the sim and block until its gRPC server answers (the endpoint can be up
/// before gRPC is ready, so retry the actual call). TEMPO_SERVER_SOCKET selects the Unix domain
/// socket transport, as it does for the sim; otherwise the client connects over TCP.
fn connect() {
    let socket = std::env::var("TEMPO_SERVER_SOCKET").unwrap_or_default();
    if socket.trim().is_empty() {
        let port: u16 = std::env::var("TEMPO_SERVER_PORT")
            .ok()
            .and_then(|s| s.parse().ok())
            .unwrap_or(10001);
        tempo_sim::set_server("localhost", port);
    } else {
        tempo_sim::set_socket(socket.trim());
    }

    let deadline = Instant::now() + Duration::from_secs(120);
    loop {
        match tempo_sim::tempo_core::get_current_level_name() {
            Ok(_) => return,
            Err(e) => {
                if Instant::now() >= deadline {
                    panic!("sim gRPC server not ready: {e}");
                }
                std::thread::sleep(Duration::from_millis(500));
            }
        }
    }
}

#[test]
fn level_name_is_reported() {
    connect();
    let resp = tempo_sim::tempo_core::get_current_level_name().expect("get_current_level_name");
    assert!(!resp.level.is_empty(), "expected a non-empty current level name");
}

/// A spawned StaticMeshActor, destroyed when the test ends, however it ends. An engine class, so
/// always available, and it spawns with no mesh or collision, so the requested pose is preserved.
struct Spawned(String);

impl Spawned {
    fn at(transform: Transform) -> Self {
        let spawned = tempo_world::spawn_actor(tempo_world::SpawnActorRequest {
            actor_type: "StaticMeshActor".into(),
            transform: Some(transform),
            ..Default::default()
        })
        .expect("spawn_actor");
        assert!(!spawned.name.is_empty(), "spawn_actor returned an empty name");
        Spawned(spawned.name)
    }

    /// Make the root movable: a spawned StaticMeshActor's is Static, which refuses a move.
    fn make_movable(&self) {
        tempo_world::set_property(tempo_world::SetPropertyRequest {
            actor: self.0.clone(),
            component: "StaticMeshComponent0".into(),
            property: "Mobility".into(),
            value: Some(Value { value: Some(V::EnumValue("Movable".into())) }),
            ..Default::default()
        })
        .expect("make the root movable");
    }

    fn root_property(&self, property: &str) -> Value {
        tempo_world::get_property(tempo_world::GetPropertyRequest {
            actor: self.0.clone(),
            component: "StaticMeshComponent0".into(),
            property: property.into(),
            ..Default::default()
        })
        .unwrap_or_else(|error| panic!("get_property {property}: {error}"))
    }
}

impl Drop for Spawned {
    fn drop(&mut self) {
        let request = tempo_world::DestroyActorRequest { actor: self.0.clone() };
        let _ = tempo_world::destroy_actor(request);
    }
}

fn close(a: f64, b: f64, tolerance: f64) -> bool {
    (a - b).abs() < tolerance
}

#[test]
fn spawn_query_destroy_round_trip() {
    connect();

    let transform = Transform {
        location: Some(Vector { x: 1.0, y: 2.0, z: 3.0 }),
        ..Default::default()
    };
    let spawned = tempo_world::spawn_actor(tempo_world::SpawnActorRequest {
        actor_type: "StaticMeshActor".into(),
        transform: Some(transform),
        ..Default::default()
    })
    .expect("spawn_actor");
    assert!(!spawned.name.is_empty(), "spawn_actor returned an empty name");

    let state = tempo_world::get_current_actor_state(tempo_world::ActorStateRequest {
        actor: spawned.name.clone(),
        ..Default::default()
    })
    .expect("get_current_actor_state");
    let loc = state
        .transform
        .expect("actor state has a transform")
        .location
        .expect("transform has a location");
    assert!((loc.x - 1.0).abs() < 0.05, "x was {}", loc.x);
    assert!((loc.y - 2.0).abs() < 0.05, "y was {} (handedness round trip)", loc.y);
    assert!((loc.z - 3.0).abs() < 0.05, "z was {}", loc.z);

    tempo_world::destroy_actor(tempo_world::DestroyActorRequest { actor: spawned.name })
        .expect("destroy_actor");
}

#[test]
fn a_property_reads_in_unreal_units() {
    connect();
    let actor = Spawned::at(Transform {
        location: Some(Vector { x: 1.0, y: 2.0, z: 3.0 }),
        ..Default::default()
    });

    // The API spawned it at (1, 2, 3) m right-handed; the property holds Unreal's own centimeters,
    // left-handed, and a Value reports exactly that.
    match actor.root_property("RelativeLocation").value {
        Some(V::VectorValue(v)) => {
            assert!(
                close(v.x, 100.0, 1e-2) && close(v.y, -200.0, 1e-2) && close(v.z, 300.0, 1e-2),
                "{v:?}"
            );
        }
        other => panic!("expected a vector, got {other:?}"),
    }
}

#[test]
fn a_rotator_property_is_written_in_degrees() {
    connect();
    let actor = Spawned::at(Transform::default());
    actor.make_movable();

    // An FRotator's roll, pitch and yaw in degrees, left-handed: a 90-degree Unreal yaw.
    tempo_world::set_property(tempo_world::SetPropertyRequest {
        actor: actor.0.clone(),
        component: "StaticMeshComponent0".into(),
        property: "RelativeRotation".into(),
        value: Some(Value { value: Some(V::RotatorValue(Rotation { r: 0.0, p: 0.0, y: 90.0 })) }),
        ..Default::default()
    })
    .expect("set RelativeRotation");

    // Read back as written, still in degrees. A raw property write does not re-derive the
    // component's world transform, so a converting getter would not see it.
    match actor.root_property("RelativeRotation").value {
        Some(V::RotatorValue(rotation)) => assert!(close(rotation.y, 90.0, 1e-3), "{rotation:?}"),
        other => panic!("expected a rotator, got {other:?}"),
    }

    // The deprecated typed setter still takes right-handed radians: the same rotation is a yaw of
    // -pi/2 there.
    #[allow(deprecated)]
    tempo_world::set_rotator_property(tempo_world::SetRotatorPropertyRequest {
        actor: actor.0.clone(),
        component: "StaticMeshComponent0".into(),
        property: "RelativeRotation".into(),
        y: -std::f32::consts::FRAC_PI_2,
        ..Default::default()
    })
    .expect("set_rotator_property");
    match actor.root_property("RelativeRotation").value {
        Some(V::RotatorValue(rotation)) => assert!(close(rotation.y, 90.0, 1e-3), "{rotation:?}"),
        other => panic!("expected a rotator, got {other:?}"),
    }
}

#[test]
fn a_transform_property_reads_as_a_native_transform() {
    connect();
    let actor = Spawned::at(Transform {
        location: Some(Vector { x: 1.0, y: 0.0, z: 0.0 }),
        ..Default::default()
    });

    match tempo_world::call_function(tempo_world::CallFunctionRequest {
        actor: actor.0.clone(),
        function: "GetTransform".into(),
        ..Default::default()
    })
    .expect("GetTransform")
    .results
    .iter()
    .find(|result| result.name == "ReturnValue")
    .and_then(|result| result.typed_value.clone())
    .and_then(|value| value.value)
    {
        Some(V::NativeTransformValue(t)) => {
            let translation = t.translation.expect("a translation");
            assert!(close(translation.x, 100.0, 1e-2), "{translation:?}");
            let scale = t.scale3d.expect("a scale");
            assert_eq!((scale.x, scale.y, scale.z), (1.0, 1.0, 1.0));
        }
        other => panic!("expected a native transform, got {other:?}"),
    }
}

#[test]
fn a_transform_carries_its_scale() {
    connect();
    let actor = Spawned::at(Transform::default());
    actor.make_movable();

    tempo_world::set_actor_transform(tempo_world::SetActorTransformRequest {
        actor: actor.0.clone(),
        transform: Some(Transform {
            location: Some(Vector { x: 0.5, y: 0.0, z: 0.0 }),
            scale: Some(Vector { x: 2.0, y: 3.0, z: 4.0 }),
            ..Default::default()
        }),
        ..Default::default()
    })
    .expect("set_actor_transform");

    let transform = tempo_world::get_actor_transform(tempo_world::GetActorTransformRequest {
        actor: actor.0.clone(),
        ..Default::default()
    })
    .expect("get_actor_transform")
    .transform
    .expect("a transform");
    let scale = transform.scale.expect("a reported transform always has a scale");
    assert!(
        close(scale.x, 2.0, 1e-4) && close(scale.y, 3.0, 1e-4) && close(scale.z, 4.0, 1e-4),
        "{scale:?}"
    );
    let location = transform.location.expect("a location");
    assert!(close(location.x, 0.5, 1e-3), "{location:?}");
}
