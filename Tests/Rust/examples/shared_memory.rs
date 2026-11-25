// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Checks the Rust client's shared-memory transport against a rendering sim on this machine: streams
//! one camera through the generated wrapper, which asks for shared memory, and through the raw
//! client, which does not, and compares the captures both received byte for byte. Not part of a
//! test group, since the integration groups run the sim without rendering.
//!
//!     TEMPO_SERVER_PORT=10001 cargo run --example shared_memory

use std::time::Duration;

use tempo_sim::proto::tempo_core::{ExecConsoleCommandRequest, SetConsoleVariableRequest};
use tempo_sim::proto::tempo_sensors::sensor_service_client::SensorServiceClient;
use tempo_sim::proto::tempo_sensors::{ColorImage, ColorImageRequest};
use tempo_sim::proto::tempo_world::{DestroyActorRequest, SpawnActorRequest};
use tempo_sim::{tempo_core, tempo_sensors, tempo_world};

const CAPTURES: usize = 6;

fn set_cvar(name: &str, value: &str) {
    tempo_core::set_console_variable(SetConsoleVariableRequest { name: name.into(), value: value.into() })
        .expect("set_console_variable");
}

fn main() {
    let port: u16 = std::env::var("TEMPO_SERVER_PORT").ok().and_then(|s| s.parse().ok()).unwrap_or(10001);
    tempo_sim::set_server("localhost", port);
    tempo_sim::set_request_timeout(Some(Duration::from_secs(30)));

    let owner = tempo_world::spawn_actor(SpawnActorRequest { actor_type: "BP_SensorRig".into(), ..Default::default() })
        .expect("spawn_actor")
        .name;
    let sensors = tempo_sensors::get_available_sensors().expect("get_available_sensors").available_sensors;
    let camera = sensors.iter().find(|s| s.owner == owner).expect("a camera on the rig").name.clone();
    let request = ColorImageRequest { owner: owner.clone(), sensor: camera };

    set_cvar("Tempo.WriteStats", "1");
    let (shared, inline) = tempo_sim::context::RUNTIME.block_on(async {
        let mut shared = tempo_sensors::stream_color_images_async(request.clone()).await.expect("shared stream");
        let channel = tempo_sim::context::connected_channel().await.expect("channel");
        let mut inline = SensorServiceClient::new(channel)
            .max_decoding_message_size(tempo_sim::context::MAX_MESSAGE_SIZE)
            .stream_color_images(request)
            .await
            .expect("inline stream")
            .into_inner();
        let mut shared_images: Vec<ColorImage> = Vec::new();
        let mut inline_images: Vec<ColorImage> = Vec::new();
        // Read them in turn, so neither stream falls behind the other.
        while shared_images.len() < CAPTURES {
            shared_images.push(shared.message().await.expect("shared image").expect("shared stream ended"));
            inline_images.push(inline.message().await.expect("inline image").expect("inline stream ended"));
        }
        (shared_images, inline_images)
    });
    let stats = tempo_core::exec_console_command(ExecConsoleCommandRequest { command: "Tempo.WriteStats.Dump".into() })
        .expect("Tempo.WriteStats.Dump")
        .output;
    set_cvar("Tempo.WriteStats", "0");
    let _ = tempo_world::destroy_actor(DestroyActorRequest { actor: owner });

    let mut compared = 0;
    for mut image in shared {
        let id = image.header.as_ref().map(|h| h.sequence_id);
        if let Some(mut other) = inline.iter().find(|i| i.header.as_ref().map(|h| h.sequence_id) == id).cloned() {
            // When each stream's response was placed on the wire may differ; nothing else may.
            image.header.as_mut().unwrap().transmission_time_s = 0.0;
            other.header.as_mut().unwrap().transmission_time_s = 0.0;
            assert_eq!(image, other, "capture {id:?} differs between shared memory and inline");
            compared += 1;
        }
    }
    assert!(compared > 0, "the two streams shared no capture");
    let shm_messages = stats
        .split("\"shm_messages\":")
        .nth(1)
        .and_then(|rest| rest.split(',').next())
        .and_then(|n| n.parse::<i64>().ok())
        .unwrap_or(0);
    assert!(shm_messages > 0, "nothing went through shared memory: {stats}");
    println!("{compared} captures identical; {shm_messages} messages went through shared memory");
}
