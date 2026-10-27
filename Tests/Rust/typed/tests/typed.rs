// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Integration tests for the reflected Rust API against a packaged sim.
//!
//! The Rust half of what `Tests/Python/test_typed_world.py` does. `Scripts/TestRustAPI.sh typed`
//! vendors the project's crate, which carries this build's generated classes as its `reflected`
//! module, as `project`, and launches the packaged sim.
//!
//! Everything here addresses classes that any host project with Tempo has -- engine classes and
//! Tempo's own -- so the file is not specific to TempoSample. Run single-threaded, since these
//! share the client's global connection context and drive the same sim.

use std::time::{Duration, Instant};

use project::reflected::classes::engine::{
    Actor, SceneComponent, StaticMeshActor, StaticMeshComponent,
};
use project::reflected::enums::EWalkableSlopeBehavior;
use project::reflected::structs::FWalkableSlopeOverride;
use tempo_sim::conversion::{quat_of, rotator_of, ue_to_si};
use tempo_sim::proto::tempo_world::{value, Value};
use tempo_sim::unreal::{Class, FRotator, FTransform, FVector};
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
        let request = tempo_sim::tempo_world::DestroyActorRequest { actor: self.name() };
        let _ = tempo_sim::tempo_world::destroy_actor(request);
    }
}

/// Make the actor's root movable. A spawned StaticMeshActor's root is Static, which refuses both a
/// move and a new mesh -- correctly, and with a false return that the tests below are not about.
fn make_movable(actor: &Spawned) {
    // The root's own handle, typed, would do this as well; the generic RPC is what a scenario
    // without the reflected API would write.
    tempo_sim::tempo_world::set_property(tempo_sim::tempo_world::SetPropertyRequest {
        actor: actor.name(),
        component: "StaticMeshComponent0".into(),
        property: "Mobility".into(),
        value: Some(Value { value: Some(value::Value::EnumValue("Movable".into())) }),
        ..Default::default()
    })
    .expect("make the root movable");
}

fn spawn() -> Spawned {
    // StaticMeshActor is an engine class, so it is present whatever the host project is, and it
    // spawns with no mesh or collision so nothing moves it afterwards.
    Spawned(world::spawn_actor::<StaticMeshActor>(None, None).expect("spawn_actor"))
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
    let _: FVector = component.relative_location().expect("read RelativeLocation");
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
fn a_reference_read_from_a_property_knows_its_class() {
    connect();
    let actor = spawn();

    // RootComponent carries a BlueprintGetter, K2_GetRootComponent, so this is a call rather than a
    // read of the property's memory. The wire names the component and nothing else, and the handle
    // is typed by the declared class -- so is_a agrees with the type system about what came back.
    let root = actor.actor().root_component().expect("read RootComponent");
    assert_eq!(root.as_object().actor_name(), actor.name());
    assert_eq!(root.as_object().component_name(), "StaticMeshComponent0");
    assert!(root.as_object().is_a(SceneComponent::PATH), "a handle typed SceneComponent is one");
    // And the same handle reaches the component's own properties.
    let _: FVector = root.relative_location().expect("read RelativeLocation through the reference");
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
    let wrong = world::get_actor::<project::reflected::classes::engine::Pawn>(&name);
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

#[test]
fn an_actor_spawned_deferred_without_blocking_is_configured_before_it_begins() {
    connect();
    // The handle is kept past block_on, where Spawned's blocking destroy can run.
    let actor = tempo_sim::context::RUNTIME.block_on(async {
        let actor = world::spawn_actor_deferred_async::<StaticMeshActor, _, _>(
            None,
            None,
            |actor| async move {
                // Owned by the future, as the async form hands it over; a batch queues the writes
                // and sends them as one, before FinishSpawning lets the actor begin.
                world::batch_async(|| {
                    actor.set_b_can_be_damaged(false)?;
                    actor.set_b_always_relevant(true)
                })
                .await
            },
        )
        .await
        .expect("spawn_actor_deferred_async");
        Spawned(actor)
    });
    assert!(!actor.actor().b_can_be_damaged().expect("read bCanBeDamaged"));
    assert!(actor.actor().b_always_relevant().expect("read bAlwaysRelevant"));
    assert!(
        world::get_actors::<StaticMeshActor>()
            .expect("get_actors")
            .iter()
            .any(|candidate| candidate.as_object().actor_name() == actor.name()),
        "the actor finished spawning"
    );
}

#[test]
fn a_struct_argument_and_result_round_trip() {
    connect();
    let actor = spawn();
    let component = actor.actor().static_mesh_component0();

    // In through a parameter and out through a return value: one generated value type both ways.
    component
        .set_walkable_slope_override(FWalkableSlopeOverride {
            walkable_slope_behavior: Some(EWalkableSlopeBehavior::WalkableSlope_Decrease),
            walkable_slope_angle: Some(12.0),
        })
        .expect("SetWalkableSlopeOverride");
    let back = component.get_walkable_slope_override().expect("GetWalkableSlopeOverride");
    assert_eq!(back.walkable_slope_behavior, Some(EWalkableSlopeBehavior::WalkableSlope_Decrease));
    assert!((back.walkable_slope_angle.expect("the angle comes back") - 12.0).abs() < 1e-3);
}

#[test]
fn an_out_struct_comes_back_with_the_return_value() {
    connect();
    let actor = spawn();
    make_movable(&actor);

    // K2_SetActorLocation's FHitResult had no representation before struct values, so the method
    // returned only its bool. It is now a value, read back by name.
    let (moved, hit) = actor
        .actor()
        .k2_set_actor_location(FVector { x: 100.0, y: 0.0, z: 0.0 }, false, true)
        .expect("K2_SetActorLocation");
    assert!(moved);
    assert_eq!(hit.b_blocking_hit, Some(false), "nothing to hit with the sweep off");
}

#[test]
fn a_transform_travels_in_unreals_shape() {
    connect();
    let actor = spawn();
    make_movable(&actor);

    // An FTransform argument and result keep their quaternion and scale. A typed write used to
    // reset the scale to 1, and a read to drop it.
    let wanted = FTransform {
        rotation: quat_of(&FRotator { pitch: 0.0, yaw: 90.0, roll: 0.0 }),
        translation: FVector { x: 100.0, y: 0.0, z: 50.0 },
        scale3d: FVector { x: 2.0, y: 3.0, z: 4.0 },
    };
    let (moved, _) = actor
        .actor()
        .k2_set_actor_transform(wanted, false, true)
        .expect("K2_SetActorTransform");
    assert!(moved);
    let got = actor.actor().get_transform().expect("GetTransform");
    let close = |a: f64, b: f64| (a - b).abs() < 1e-3;
    assert!(
        close(got.scale3d.x, 2.0) && close(got.scale3d.y, 3.0) && close(got.scale3d.z, 4.0),
        "the scale survives the round trip: {got:?}"
    );
    assert!(close(got.translation.x, 100.0) && close(got.translation.z, 50.0), "{got:?}");
    assert!(close(rotator_of(&got.rotation).yaw, 90.0), "{got:?}");
}

#[test]
fn a_transform_set_in_si_carries_its_scale_and_converts_back() {
    connect();
    let actor = spawn();
    make_movable(&actor);

    // The scale rides in the transform, used as given.
    world::set_actor_transform(
        actor.actor(),
        world::Transform {
            location: Some(world::Vector { x: 1.0, y: 2.0, z: 0.5 }),
            rotation: Some(world::Rotation { r: 0.0, p: 0.0, y: 0.5 }),
            scale: Some(world::Vector { x: 2.0, y: 3.0, z: 4.0 }),
        },
        None,
    )
    .expect("set_actor_transform");
    let reported = world::get_actor_transform(actor.actor(), None)
        .expect("get_actor_transform")
        .transform
        .expect("a transform");

    // The generated class reads the same transform in Unreal's shape, and converting it gives what
    // the converting RPC reported.
    let converted = ue_to_si(&actor.actor().get_transform().expect("GetTransform"));
    let close = |a: f64, b: f64| (a - b).abs() < 1e-3;
    let (scale, back) = (reported.scale.expect("a scale"), converted.scale.expect("a scale"));
    assert!(close(scale.x, 2.0) && close(scale.y, 3.0) && close(scale.z, 4.0), "{scale:?}");
    assert!(close(back.x, scale.x) && close(back.y, scale.y) && close(back.z, scale.z), "{back:?}");
    let location = reported.location.expect("a location");
    let back = converted.location.expect("a location");
    assert!(close(back.x, location.x) && close(back.y, location.y) && close(back.z, location.z));
    let rotation = reported.rotation.expect("a rotation");
    let back = converted.rotation.expect("a rotation");
    assert!(close(rotation.y, 0.5) && close(back.y, rotation.y), "{rotation:?} vs {back:?}");
}

#[test]
fn the_world_mirror_works_on_handles() {
    connect();
    let actor = spawn();
    make_movable(&actor);

    // world is tempo_world given handles: add a component and find it by class, move an actor in SI
    // and read it back, then take the component away.
    let added: StaticMeshComponent =
        world::add_component(actor.actor(), "MirrorMesh", None, None, "").expect("add_component");
    let names = |found: Vec<StaticMeshComponent>| -> Vec<String> {
        found.iter().map(|c| c.as_object().component_name().to_string()).collect()
    };
    let before =
        names(world::get_components::<StaticMeshComponent>(actor.actor()).expect("get_components"));
    assert!(before.contains(&added.as_object().component_name().to_string()), "{before:?}");

    world::set_actor_location(actor.actor(), world::Vector { x: 2.0, y: 1.0, z: 0.5 }, None)
        .expect("set_actor_location");
    let location = world::get_actor_location(actor.actor(), None)
        .expect("get_actor_location")
        .location
        .expect("a location");
    assert!((location.x - 2.0).abs() < 1e-4 && (location.y - 1.0).abs() < 1e-4, "{location:?}");
    // The same place in Unreal's terms, through the generated class.
    let unreal = actor.actor().k2_get_actor_location().expect("K2_GetActorLocation");
    assert!((unreal.y + 100.0).abs() < 1e-2, "{unreal:?}");

    world::destroy_component(&added).expect("destroy_component");
    let after =
        names(world::get_components::<StaticMeshComponent>(actor.actor()).expect("get_components"));
    assert!(!after.contains(&added.as_object().component_name().to_string()), "{after:?}");
}

#[test]
fn the_async_world_mirror_finds_and_streams() {
    connect();
    let actor = spawn();
    let name = actor.name();

    tempo_sim::context::RUNTIME.block_on(async {
        // find_actor's async twin: the escape hatch, typed by nothing but carrying the real class.
        let found = world::find_actor_async(&name).await.expect("find_actor_async");
        assert_eq!(found.actor_name(), name);
        assert!(found.is_a(StaticMeshActor::PATH));
        assert!(world::find_actor_async("NoSuchActorExistsHere").await.is_err());

        // The stream mirrors hand back tonic's stream for `.message().await`; the sim ticks on its
        // own clock under -nopause, so the first state arrives with the next tick.
        let mut states = world::stream_actor_state_async(actor.actor(), false)
            .await
            .expect("stream_actor_state_async");
        let state = states.message().await.expect("the stream stays open").expect("a state");
        assert_eq!(state.name, name);

        let mut nearby = world::stream_actor_states_near_async(actor.actor(), 10.0, true, false, false)
            .await
            .expect("stream_actor_states_near_async");
        let states = nearby.message().await.expect("the stream stays open").expect("some states");
        assert!(
            states.actor_states.iter().any(|state| state.name == name),
            "an actor is near itself: {:?}",
            states.actor_states.iter().map(|state| &state.name).collect::<Vec<_>>()
        );
    });
}

#[test]
fn a_batch_defers_reads_and_keeps_order() {
    connect();
    let actor = spawn();

    let mut damaged = None;
    let mut frequency = None;
    world::batch(|| {
        actor.actor().set_b_can_be_damaged(false)?;
        // A BlueprintSetter write: a call, in the same RunBatch.
        actor.actor().set_net_update_frequency(40.0)?;
        damaged = Some(world::defer(|| actor.actor().b_can_be_damaged())?);
        frequency = Some(world::defer(|| actor.actor().net_update_frequency())?);
        Ok(())
    })
    .expect("batch");
    assert!(!damaged.expect("deferred").take().expect("bCanBeDamaged"));
    assert_eq!(frequency.expect("deferred").take().expect("NetUpdateFrequency"), 40.0);

    world::batch(|| {
        actor.actor().set_b_can_be_damaged(true)?;
        // Read directly, so it goes out with the write before it rather than ahead of it.
        assert!(actor.actor().b_can_be_damaged()?, "an immediate read sees the queued write");
        Ok(())
    })
    .expect("batch");
}

#[test]
fn async_code_batches_and_reaches_struct_members() {
    connect();
    let actor = spawn();
    let component = actor.actor().static_mesh_component0();

    tempo_sim::context::RUNTIME.block_on(async {
        let mut damaged = None;
        world::batch_async(|| {
            actor.actor().set_b_can_be_damaged(false)?;
            actor.actor().set_net_update_frequency(30.0)?;
            component.body_instance().set_linear_damping(0.25)?;
            damaged = Some(world::defer(|| actor.actor().b_can_be_damaged())?);
            Ok(())
        })
        .await
        .expect("batch_async");
        assert!(!damaged.expect("deferred").take().expect("bCanBeDamaged"));
        assert_eq!(actor.actor().net_update_frequency_async().await.expect("read"), 30.0);

        // A member of a struct property, written and read without blocking.
        let body = component.body_instance();
        body.set_linear_damping_async(0.5).await.expect("set LinearDamping");
        let damping = body.linear_damping_async().await.expect("read LinearDamping");
        assert!((damping - 0.5).abs() < 1e-4);
    });
}

#[test]
fn a_catalog_asset_goes_through_a_typed_setter() {
    use project::reflected::assets::engine::basic_shapes::Cube;

    connect();
    let actor = spawn();
    make_movable(&actor);
    let component = actor.actor().static_mesh_component0();

    // An AssetRef<StaticMesh> from the catalog, into the setter that takes one. A material here
    // would not compile.
    assert!(component.set_static_mesh(&Cube).expect("SetStaticMesh"));
    assert_eq!(component.static_mesh().expect("read StaticMesh"), Cube.clone());
}

#[test]
fn a_subsystem_is_reached_by_its_class() {
    use project::reflected::classes::engine::ReplaySubsystem;
    use project::reflected::classes::tempo_agents::TempoAgentsWorldSubsystem;

    connect();
    // A world subsystem, read and written; there is one per world, so the class is the address.
    let agents = world::get_subsystem::<TempoAgentsWorldSubsystem>().expect("get_subsystem");
    let before = agents.brightness_meter_lateral_offset().expect("read");
    agents.set_brightness_meter_lateral_offset(456.0).expect("write");
    let after = agents.brightness_meter_lateral_offset().expect("read back");
    agents.set_brightness_meter_lateral_offset(before).expect("restore");
    assert_eq!(after, 456.0);

    // A game instance subsystem, through a function.
    let replay = world::get_subsystem::<ReplaySubsystem>().expect("get_subsystem");
    assert!(!replay.is_playing().expect("IsPlaying"), "nothing is replaying in a test sim");
}

#[test]
fn a_class_that_is_not_a_subsystem_fails_at_the_lookup() {
    connect();
    assert!(world::get_subsystem::<StaticMeshActor>().is_err());
}
