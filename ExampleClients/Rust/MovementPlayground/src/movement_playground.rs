// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Movement Playground - interactive TempoMovement client (Rust port of
//! MovementPlayground.py).
//!
//! Drive vehicles, command velocities/accelerations, and navigate pawns at
//! runtime.

use clap::Parser;
use inquire::{Select, Text};
use tempo_sim::proto::tempo_core::{Accel, Twist, Vector};
use tempo_sim::proto::tempo_movement::MoveToResult;
use tempo_sim::{default_unix_socket_path, set_server_async, set_unix_socket_async, tempo_movement};

#[derive(Parser, Debug)]
struct Args {
    #[arg(long, help = "IP address of machine where Tempo is running. If unset, connect via Unix domain socket.")]
    ip: Option<String>,
    #[arg(long, default_value_t = 10001u16, help = "Port Tempo gRPC server is using")]
    port: u16,
}

// ---------------------------------------------------------------------------
// Prompt helpers
// ---------------------------------------------------------------------------

async fn select_one(prompt: &str, options: Vec<String>) -> Option<String> {
    if options.is_empty() {
        return None;
    }
    let prompt = prompt.to_string();
    tokio::task::spawn_blocking(move || Select::new(&prompt, options).prompt().ok())
        .await
        .ok()
        .flatten()
}

async fn text_input(prompt: &str, default: &str) -> Option<String> {
    let prompt = prompt.to_string();
    let default = default.to_string();
    tokio::task::spawn_blocking(move || {
        if default.is_empty() {
            Text::new(&prompt).prompt().ok()
        } else {
            Text::new(&prompt).with_default(&default).prompt().ok()
        }
    })
    .await
    .ok()
    .flatten()
}

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

fn parse_floats(text: &str, count: usize, label: &str) -> Result<Vec<f64>, String> {
    let parts: Vec<f64> = text
        .split_whitespace()
        .map(|s| s.parse::<f64>())
        .collect::<Result<_, _>>()
        .map_err(|e| format!("Failed to parse number: {}", e))?;
    if parts.len() != count {
        return Err(format!("Expected {} values: {}", count, label));
    }
    Ok(parts)
}

fn move_to_result_string(result: i32) -> &'static str {
    match MoveToResult::try_from(result).unwrap_or(MoveToResult::MtrUnknown) {
        MoveToResult::MtrUnknown => "UNKNOWN",
        MoveToResult::MtrSuccess => "SUCCESS",
        MoveToResult::MtrBlocked => "BLOCKED",
        MoveToResult::MtrOffPath => "OFF_PATH",
        MoveToResult::MtrAborted => "ABORTED",
        MoveToResult::MtrInvalid => "INVALID",
    }
}

// ---------------------------------------------------------------------------
// Pawn fetchers
// ---------------------------------------------------------------------------

async fn fetch_commandable_pawns() -> Vec<String> {
    match tempo_movement::get_commandable_pawns_async().await {
        Ok(r) => r.pawns,
        Err(e) => {
            println!("\n  Error fetching commandable pawns: {}", e);
            Vec::new()
        }
    }
}

async fn fetch_navigable_pawns() -> Vec<String> {
    match tempo_movement::get_navigable_pawns_async().await {
        Ok(r) => r.pawns,
        Err(e) => {
            println!("\n  Error fetching navigable pawns: {}", e);
            Vec::new()
        }
    }
}

async fn pick_pawn(prompt: &str, pawns: Vec<String>) -> Option<String> {
    if pawns.is_empty() {
        println!("  No pawns available.");
        return None;
    }
    select_one(prompt, pawns).await
}

// ---------------------------------------------------------------------------
// Flows
// ---------------------------------------------------------------------------

async fn flow_list_commandable() {
    println!("\n--- Commandable Pawns ---");
    let pawns = fetch_commandable_pawns().await;
    if pawns.is_empty() {
        println!("  (none)");
        return;
    }
    for p in pawns {
        println!("  {}", p);
    }
}

async fn flow_list_navigable() {
    println!("\n--- Navigable Pawns ---");
    let pawns = fetch_navigable_pawns().await;
    if pawns.is_empty() {
        println!("  (none)");
        return;
    }
    for p in pawns {
        println!("  {}", p);
    }
}

async fn flow_command_vehicle() {
    println!("\n--- Command Vehicle (Normalized Driving) ---");
    let pawns = fetch_commandable_pawns().await;
    let Some(pawn) = pick_pawn("Which vehicle?", pawns).await else {
        return;
    };

    let Some(accel_text) = text_input("Acceleration [-1, 1] (throttle/brake)", "0.0").await else {
        return;
    };
    let Some(steer_text) = text_input("Steering [-1, 1] (left/right)", "0.0").await else {
        return;
    };
    let acceleration: f32 = match accel_text.parse() {
        Ok(v) => v,
        Err(e) => {
            println!("  {}", e);
            return;
        }
    };
    let steering: f32 = match steer_text.parse() {
        Ok(v) => v,
        Err(e) => {
            println!("  {}", e);
            return;
        }
    };

    match tempo_movement::command_vehicle_async(pawn.clone(), acceleration, steering).await {
        Ok(_) => println!(
            "\n  Sent NormalizedDrivingCommand to {}: accel={:.3}, steer={:.3}",
            pawn, acceleration, steering
        ),
        Err(e) => println!("  Error commanding vehicle: {}", e),
    }
}

async fn flow_command_velocity() {
    println!("\n--- Command Velocity (body frame: linear m/s, angular rad/s) ---");
    let pawns = fetch_commandable_pawns().await;
    let Some(pawn) = pick_pawn("Which pawn?", pawns).await else {
        return;
    };

    let Some(text) = text_input(
        "Twist (linear X Y Z, angular X Y Z; X forward, Y right, Z up)",
        "0 0 0 0 0 0",
    )
    .await
    else {
        return;
    };
    let v = match parse_floats(&text, 6, "linear X Y Z angular X Y Z") {
        Ok(v) => v,
        Err(e) => {
            println!("  {}", e);
            return;
        }
    };

    let twist = Twist {
        linear: Some(Vector { x: v[0], y: v[1], z: v[2] }),
        angular: Some(Vector { x: v[3], y: v[4], z: v[5] }),
    };

    match tempo_movement::command_velocity_async(pawn.clone(), twist).await {
        Ok(_) => println!("\n  Sent VelocityCommand to {}", pawn),
        Err(e) => println!("  Error commanding velocity: {}", e),
    }
}

async fn flow_command_acceleration() {
    println!("\n--- Command Acceleration (body frame: linear m/s^2, angular rad/s^2) ---");
    let pawns = fetch_commandable_pawns().await;
    let Some(pawn) = pick_pawn("Which pawn?", pawns).await else {
        return;
    };

    let Some(text) = text_input(
        "Accel (linear X Y Z, angular X Y Z; X forward, Y right, Z up)",
        "0 0 0 0 0 0",
    )
    .await
    else {
        return;
    };
    let v = match parse_floats(&text, 6, "linear X Y Z angular X Y Z") {
        Ok(v) => v,
        Err(e) => {
            println!("  {}", e);
            return;
        }
    };

    let accel = Accel {
        linear: Some(Vector { x: v[0], y: v[1], z: v[2] }),
        angular: Some(Vector { x: v[3], y: v[4], z: v[5] }),
    };

    match tempo_movement::command_acceleration_async(pawn.clone(), accel).await {
        Ok(_) => println!("\n  Sent AccelerationCommand to {}", pawn),
        Err(e) => println!("  Error commanding acceleration: {}", e),
    }
}

async fn flow_move_to_location() {
    println!("\n--- Pawn Move To Location ---");
    let pawns = fetch_navigable_pawns().await;
    let Some(pawn) = pick_pawn("Which pawn?", pawns).await else {
        return;
    };

    let mode_options = vec![
        "Absolute (world frame)".to_string(),
        "Relative (offset from pawn's current location)".to_string(),
    ];
    let Some(mode) = select_one("Coordinate mode?", mode_options).await else {
        return;
    };
    let relative = mode.starts_with("Relative");

    let Some(text) = text_input(
        "Target X Y Z in meters, separated by spaces (e.g. 5 0 0)",
        "0 0 0",
    )
    .await
    else {
        return;
    };
    let xyz = match parse_floats(&text, 3, "X Y Z") {
        Ok(v) => v,
        Err(e) => {
            println!("  {}", e);
            return;
        }
    };

    let location = Vector { x: xyz[0], y: xyz[1], z: xyz[2] };

    println!(
        "\n  Moving {} to ({:.2}, {:.2}, {:.2}) ({}). Waiting for navigation to finish...",
        pawn,
        xyz[0],
        xyz[1],
        xyz[2],
        if relative { "relative" } else { "absolute" }
    );
    match tempo_movement::pawn_move_to_location_async(pawn, location, relative).await {
        Ok(resp) => println!("  Result: {}", move_to_result_string(resp.result)),
        Err(e) => println!("  Error during move-to-location: {}", e),
    }
}

async fn flow_rebuild_navigation() {
    println!("\n--- Rebuild Navigation ---");
    match tempo_movement::rebuild_navigation_async().await {
        Ok(_) => println!("  Navigation rebuilt."),
        Err(e) => println!("  Error rebuilding navigation: {}", e),
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

#[tokio::main]
async fn main() {
    let args = Args::parse();
    match args.ip {
        Some(ip) => set_server_async(&ip, args.port).await,
        None => {
            if args.port != 10001 {
                set_unix_socket_async(default_unix_socket_path(args.port)).await;
            }
        }
    }

    println!("\n=== Movement Playground ===");
    println!("Drive vehicles, command velocities/accelerations, and navigate pawns at runtime.\n");

    let actions = vec![
        "List commandable pawns",
        "List navigable pawns",
        "Command vehicle (normalized acceleration/steering)",
        "Command velocity (twist)",
        "Command acceleration",
        "Pawn move-to-location (uses navigation)",
        "Rebuild navigation",
        "Quit",
    ];

    loop {
        let options: Vec<String> = actions.iter().map(|s| s.to_string()).collect();
        let Some(action) = select_one("What would you like to do?", options).await else {
            break;
        };
        match action.as_str() {
            "List commandable pawns" => flow_list_commandable().await,
            "List navigable pawns" => flow_list_navigable().await,
            "Command vehicle (normalized acceleration/steering)" => flow_command_vehicle().await,
            "Command velocity (twist)" => flow_command_velocity().await,
            "Command acceleration" => flow_command_acceleration().await,
            "Pawn move-to-location (uses navigation)" => flow_move_to_location().await,
            "Rebuild navigation" => flow_rebuild_navigation().await,
            "Quit" => break,
            _ => {}
        }
    }

    println!("\nBye!\n");
}
