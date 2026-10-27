// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Contract test for the generated `tempo-sim` Rust crate.
//!
//! This needs no running sim: it only has to *compile and link* against the packaged crate, which
//! proves the codegen produced the expected modules, function signatures, and proto types. A
//! renamed/dropped symbol or a changed signature breaks the build (and so the `contract` group).
//! The conversion tests below are pure arithmetic, so they run here too. Run via
//! Scripts/TestRustAPI.sh contract.

use tempo_sim::conversion::{quat_of, rotator_of, si_to_ue, ue_direction_to_si, ue_to_si};
use tempo_sim::proto::tempo_core::{Empty, Transform, Vector};
use tempo_sim::proto::tempo_world::Quat;
use tempo_sim::unreal::{FQuat, FRotator, FTransform, FVector};
use tempo_sim::{tempo_movement, tempo_world, TempoError};

#[test]
fn api_surface_exists() {
    // Referencing each item as a value forces it to exist with a compatible shape.
    let _set_server = tempo_sim::set_server;
    // set_socket is generic over AsRef<Path>, so pin the type parameter to name it as a value.
    // An owned String avoids the higher-ranked lifetime a &str parameter would need.
    let _set_socket: fn(String) = tempo_sim::set_socket::<String>;
    // An RPC whose request is Empty takes nothing.
    let _level: fn() -> Result<tempo_sim::proto::tempo_core::CurrentLevelResponse, TempoError> =
        tempo_sim::tempo_core::get_current_level_name;
    let _sim_time = tempo_sim::tempo_core::get_sim_time;
    let _step: fn() -> Result<Empty, TempoError> = tempo_sim::tempo_core::step;
    // Every other wrapper takes its request message whole, re-exported beside it.
    let _spawn: fn(tempo_world::SpawnActorRequest)
        -> Result<tempo_sim::proto::tempo_world::SpawnActorResponse, TempoError> =
        tempo_world::spawn_actor;
    let _state = tempo_world::get_current_actor_state;
    let _destroy: fn(tempo_world::DestroyActorRequest) -> Result<Empty, TempoError> =
        tempo_world::destroy_actor;
    let _set_property: fn(tempo_world::SetPropertyRequest) -> Result<Empty, TempoError> =
        tempo_world::set_property;
    let _cmd_velocity: fn(tempo_movement::VelocityCommand) -> Result<Empty, TempoError> =
        tempo_movement::command_velocity;
    let _pawns = tempo_movement::get_commandable_pawns;

    // A request builds from its fields and defaults the rest, so a field added to it later breaks
    // no caller.
    let _request = tempo_world::SetPropertyRequest {
        actor: "A".into(),
        property: "P".into(),
        ..Default::default()
    };

    // The fluent builder: subsystem stays, native_units is gone.
    let _call = tempo_world::call(String::new(), String::new(), "IsPlaying".into())
        .subsystem("/Script/Engine.ReplaySubsystem".into())
        .color_arg("Tint".into(), 255, 0, 0, None);

    // Proto messages construct (prost message fields are Option<T>).
    let _t = Transform {
        location: Some(Vector { x: 1.0, y: 2.0, z: 3.0 }),
        ..Default::default()
    };
}

fn close(a: f64, b: f64) -> bool {
    (a - b).abs() < 1e-9
}

#[test]
fn a_quat_converts_exactly_and_right_handed() {
    // A 90-degree yaw: Unreal's left-handed yaw turns X toward +Y, which right-handed is -Y, so the
    // same rotation is a -90-degree yaw there -- the axis's Z negated, W kept.
    let ue = quat_of(&FRotator { pitch: 0.0, yaw: 90.0, roll: 0.0 });
    let si: Quat = ue_to_si(&ue);
    assert!(close(si.z, -ue.z) && close(si.w, ue.w), "{si:?}");

    // X and Z negated, Y and W kept: TempoConversion.h's rule, and its own inverse.
    let q = FQuat { x: 0.1, y: 0.2, z: 0.3, w: (1.0f64 - 0.14).sqrt() };
    let si = ue_to_si(&q);
    assert_eq!((si.x, si.y, si.z, si.w), (-q.x, q.y, -q.z, q.w));
    assert_eq!(si_to_ue(&si), q);
}

/// `v` rotated by the unit quaternion `(x, y, z, w)`: `v + 2w(u × v) + 2u × (u × v)`.
fn rotate(x: f64, y: f64, z: f64, w: f64, v: [f64; 3]) -> [f64; 3] {
    let u = [x, y, z];
    let cross = |a: [f64; 3], b: [f64; 3]| {
        [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    };
    let t = cross(u, v);
    let t = [2.0 * t[0], 2.0 * t[1], 2.0 * t[2]];
    let c = cross(u, t);
    [v[0] + w * t[0] + c[0], v[1] + w * t[1] + c[1], v[2] + w * t[2] + c[2]]
}

#[test]
fn a_quat_rotates_the_same_in_either_frame() {
    // What the converted quaternion does to a converted direction is what the original does to the
    // original, converted: the handedness change commutes with the rotation.
    let ue = quat_of(&FRotator { pitch: 10.0, yaw: 30.0, roll: -20.0 });
    let si = ue_to_si(&ue);
    let v = FVector { x: 0.3, y: -0.5, z: 0.8 };
    let rotated_ue = rotate(ue.x, ue.y, ue.z, ue.w, [v.x, v.y, v.z]);
    let expected =
        ue_direction_to_si(&FVector { x: rotated_ue[0], y: rotated_ue[1], z: rotated_ue[2] });
    let d = ue_direction_to_si(&v);
    let rotated_si = rotate(si.x, si.y, si.z, si.w, [d.x, d.y, d.z]);
    assert!(close(rotated_si[0], expected.x), "{rotated_si:?} vs {expected:?}");
    assert!(close(rotated_si[1], expected.y), "{rotated_si:?} vs {expected:?}");
    assert!(close(rotated_si[2], expected.z), "{rotated_si:?} vs {expected:?}");
}

#[test]
fn a_transform_carries_its_scale_unconverted() {
    let ue = FTransform {
        rotation: quat_of(&FRotator { pitch: 0.0, yaw: 45.0, roll: 0.0 }),
        translation: FVector { x: 100.0, y: 200.0, z: 300.0 },
        scale3d: FVector { x: 2.0, y: 3.0, z: 4.0 },
    };
    let si = ue_to_si(&ue);
    let scale = si.scale.clone().expect("a converted transform has a scale");
    assert_eq!((scale.x, scale.y, scale.z), (2.0, 3.0, 4.0), "a ratio is not a length");
    let location = si.location.clone().expect("a location");
    assert!(close(location.x, 1.0) && close(location.y, -2.0) && close(location.z, 3.0));

    let back = si_to_ue(&si);
    assert_eq!(back.scale3d, ue.scale3d);
    assert!(close(back.translation.x, 100.0) && close(back.translation.y, 200.0));
    assert!(close(rotator_of(&back.rotation).yaw, 45.0), "{back:?}");
}

#[test]
fn a_transform_with_no_scale_is_scale_one() {
    let si = Transform { location: Some(Vector { x: 1.0, y: 0.0, z: 0.0 }), ..Default::default() };
    let ue = si_to_ue(&si);
    assert_eq!(ue.scale3d, FVector { x: 1.0, y: 1.0, z: 1.0 });
    assert!(close(ue.translation.x, 100.0));
}
