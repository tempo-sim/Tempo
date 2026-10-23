// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Integration tests for the typed Rust class surface against a packaged sim.
//!
//! The Rust half of what `Tests/Python/test_typed_world.py` does. `Scripts/TestRustAPI.sh typed`
//! vendors this build's generated classes as `project-classes` and launches the packaged sim.
//!
//! Everything here addresses classes that any host project with Tempo has -- engine classes and
//! Tempo's own -- so the file is not specific to TempoSample. Run single-threaded, since these
//! share the client's global connection context and drive the same sim.

use std::time::{Duration, Instant};

use project_classes::classes::engine::{Actor, StaticMeshActor};
use tempo_sim::unreal::{Class, Vector};
use tempo_sim::world;

/// Point the client at the sim and block until its gRPC server answers.
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
            Err(error) => {
                if Instant::now() >= deadline {
                    panic!("sim gRPC server not ready: {error}");
                }
                std::thread::sleep(Duration::from_millis(500));
            }
        }
    }
}

/// A spawned actor that is destroyed when the test ends, however it ends.
struct Spawned(StaticMeshActor);

impl Spawned {
    /// The typed handle. Named rather than reached through `.0`, which would otherwise have to be
    /// written `.0 .0` wherever the ObjectRef underneath it is wanted.
    fn actor(&self) -> &StaticMeshActor {
        &self.0
    }

    fn name(&self) -> String {
        self.0.as_object().actor_name().to_string()
    }
}

impl Drop for Spawned {
    fn drop(&mut self) {
        let _ = tempo_sim::tempo_world::destroy_actor(self.name());
    }
}

fn spawn() -> Spawned {
    // StaticMeshActor is an engine class, so it is present whatever the host project is, and it
    // spawns with no mesh or collision so nothing moves it afterwards.
    Spawned(world::spawn_actor::<StaticMeshActor>(None, "").expect("spawn_actor"))
}

#[test]
fn the_linked_classes_match_the_sim() {
    connect();
    // The content hash covers names, types and writability. If this fails, every other test in
    // this file is testing stubs that describe a different build.
    world::check_schema().expect("the generated classes should match the sim they came from");
}

#[test]
fn a_property_round_trips_through_the_typed_surface() {
    connect();
    let actor = spawn();

    // bCanBeDamaged is declared on Actor, and the handle is a StaticMeshActor -- so this read and
    // write resolve through Deref, which is the whole reason the classes are a Deref chain.
    actor.actor().set_b_can_be_damaged(false).expect("set bCanBeDamaged");
    assert!(!actor.actor().b_can_be_damaged().expect("read bCanBeDamaged"));
    actor.actor().set_b_can_be_damaged(true).expect("set bCanBeDamaged");
    assert!(actor.actor().b_can_be_damaged().expect("read bCanBeDamaged"));
}

#[test]
fn a_property_with_a_blueprint_setter_is_written_through_it() {
    connect();
    let actor = spawn();

    // NetUpdateFrequency carries a BlueprintSetter, so the generated setter calls
    // SetNetUpdateFrequency rather than writing the memory -- which is the thing a packaged
    // harvest could not have known, and the reason generation runs in the editor. The write also
    // has to *name* its argument ("Frequency"): CallFunction refuses an unnamed one outright, and
    // every write down this path failed with FAILED_PRECONDITION until the generator resolved it.
    //
    // Not bAutoDestroyWhenFinished, which was the first choice: its setter destroyed the actor,
    // and the following read failed with "Failed to find actor". That was proof the accessor ran
    // -- writing the memory directly could not have done it -- but it makes a poor assertion.
    actor.actor().set_net_update_frequency(50.0).expect("set NetUpdateFrequency");
    assert_eq!(actor.actor().net_update_frequency().expect("read"), 50.0);
    actor.actor().set_net_update_frequency(25.0).expect("set NetUpdateFrequency");
    assert_eq!(actor.actor().net_update_frequency().expect("read"), 25.0);
}

#[test]
fn batched_writes_all_land() {
    connect();
    let actor = spawn();

    world::batch(|| {
        actor.actor().set_b_can_be_damaged(false)?;
        actor.actor().set_b_always_relevant(true)?;
        Ok(())
    })
    .expect("batch");

    assert!(!actor.actor().b_can_be_damaged().expect("read bCanBeDamaged"));
    assert!(actor.actor().b_always_relevant().expect("read bAlwaysRelevant"));
}

#[test]
fn nothing_is_sent_when_a_batch_fails() {
    connect();
    let actor = spawn();
    actor.actor().set_b_always_relevant(false).expect("set bAlwaysRelevant");

    let outcome = world::batch(|| {
        actor.actor().set_b_always_relevant(true)?;
        Err(tempo_sim::TempoError::Protocol("the scenario changed its mind".into()))
    });
    assert!(outcome.is_err(), "the batch should report the body's error");
    assert!(
        !actor.actor().b_always_relevant().expect("read bAlwaysRelevant"),
        "a batch abandoned by an error must not have sent its writes"
    );
}

#[test]
fn a_component_is_reachable_through_its_owner() {
    connect();
    let actor = spawn();

    // Typed as StaticMeshComponent, so this is its own property and not the actor's.
    let component = actor.actor().static_mesh_component0();
    let _: Vector = component.relative_location().expect("read RelativeLocation");
}

#[test]
fn discovery_by_type_finds_the_spawned_actor() {
    connect();
    let actor = spawn();
    let wanted = actor.name();

    let found = world::get_actors::<StaticMeshActor>().expect("get_actors");
    assert!(
        found.iter().any(|candidate| candidate.as_object().actor_name() == wanted),
        "get_actors::<StaticMeshActor> did not include {wanted}"
    );
}

#[test]
fn a_handle_knows_what_it_points_at() {
    connect();
    let actor = spawn();

    // A question about the simulation, not about the handle: both of these are answered from the
    // compiled-in hierarchy, which registered itself when spawn_actor made the handle.
    assert!(actor.actor().as_object().is_a(StaticMeshActor::PATH));
    assert!(actor.actor().as_object().is_a(Actor::PATH), "a StaticMeshActor is an Actor");
    assert!(
        !actor.actor().as_object().is_a("/Script/Engine.Pawn"),
        "a StaticMeshActor is not a Pawn"
    );
}

#[test]
fn an_actor_looked_up_by_the_wrong_class_is_refused() {
    connect();
    let actor = spawn();
    let name = actor.name();

    world::get_actor::<StaticMeshActor>(&name).expect("the actor is a StaticMeshActor");

    // A name is a runtime string, so this is the one place the typed API has to check at run time.
    let wrong = world::get_actor::<project_classes::classes::engine::Pawn>(&name);
    assert!(wrong.is_err(), "looking the actor up as a Pawn should fail");

    let missing = world::get_actor::<StaticMeshActor>("NoSuchActorExistsHere");
    assert!(missing.is_err(), "a name that is not there should fail");
}

#[test]
fn the_async_twin_reaches_the_same_property() {
    connect();
    let actor = spawn();

    // The client's own runtime, not a second one: the gRPC channel is created on it, and driving
    // it from elsewhere would rebuild the channel.
    tempo_sim::context::RUNTIME.block_on(async {
        actor.actor().set_b_can_be_damaged_async(false).await.expect("set bCanBeDamaged");
        assert!(!actor.actor().b_can_be_damaged_async().await.expect("read bCanBeDamaged"));
    });
}
