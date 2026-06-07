// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Contract test for the generated `tempo-sim` Rust crate.
//!
//! This needs no running sim: it only has to *compile and link* against the packaged crate, which
//! proves the codegen produced the expected modules, function signatures, and proto types. A
//! renamed/dropped symbol or a changed signature breaks the build (and so the `contract` group).
//! Run via Scripts/TestRust.sh contract.

use tempo_sim::proto::tempo_core::{Transform, Vector};

#[test]
fn api_surface_exists() {
    // Referencing each item as a value forces it to exist with a compatible shape.
    let _set_server = tempo_sim::set_server;
    let _level = tempo_sim::tempo_core::get_current_level_name;
    let _sim_time = tempo_sim::tempo_core::get_sim_time;
    let _step = tempo_sim::tempo_core::step;
    let _spawn = tempo_sim::tempo_world::spawn_actor;
    let _state = tempo_sim::tempo_world::get_current_actor_state;
    let _destroy = tempo_sim::tempo_world::destroy_actor;
    let _cmd_velocity = tempo_sim::tempo_movement::command_velocity;
    let _pawns = tempo_sim::tempo_movement::get_commandable_pawns;

    // Proto messages construct (prost message fields are Option<T>).
    let _t = Transform {
        location: Some(Vector { x: 1.0, y: 2.0, z: 3.0 }),
        ..Default::default()
    };
}
