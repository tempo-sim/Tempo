// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Interactive sensor playground client (Rust port of SensorPlayground.py).
//!
//! Add, remove, reposition, stream, and record sensors at runtime.

use std::collections::HashMap;
use std::fs;
use std::io::Write;
use std::path::PathBuf;

use clap::Parser;
use inquire::{Select, Text};
use rand::Rng;
use show_image::{ImageInfo, ImageView, WindowProxy};
use tempo_sim::proto::tempo_camera::ColorEncoding;
use tempo_sim::proto::tempo_scripting::{Rotation, Transform, Vector};
use tempo_sim::proto::tempo_sensors::MeasurementType;
use tempo_sim::{set_server_async, tempo_sensors, tempo_world, TempoError};
use tokio::task::JoinHandle;
use tokio_stream::StreamExt;

#[derive(Parser, Debug)]
struct Args {
    #[arg(long, default_value = "0.0.0.0", help = "IP address of machine where Tempo is running")]
    ip: String,
    #[arg(long, default_value_t = 10001u16, help = "Port Tempo scripting server is using")]
    port: u16,
}

#[derive(Clone, Debug)]
struct AvailableSensor {
    name: String,
    owner: String,
    measurement_types: Vec<MeasurementType>,
}

impl AvailableSensor {
    fn label(&self) -> String {
        format!("{}:{}", self.owner, self.name)
    }
}

fn measurement_type_label(m: MeasurementType) -> &'static str {
    match m {
        MeasurementType::ColorImage => "Color",
        MeasurementType::DepthImage => "Depth",
        MeasurementType::LabelImage => "Label",
        MeasurementType::LidarScan => "PointCloud",
        MeasurementType::BoundingBoxes => "BoundingBoxes",
    }
}

async fn get_available_sensors(filter: Option<&str>) -> Vec<AvailableSensor> {
    let resp = match tempo_sensors::get_available_sensors_async().await {
        Ok(r) => r,
        Err(e) => {
            eprintln!("\n  Could not connect to Tempo. Is the simulation running? ({})", e);
            return Vec::new();
        }
    };
    let mut out = Vec::new();
    for s in resp.available_sensors {
        let mts: Vec<MeasurementType> = s
            .measurement_types
            .iter()
            .filter_map(|i| MeasurementType::try_from(*i).ok())
            .collect();
        let is_camera = mts.contains(&MeasurementType::ColorImage);
        let is_lidar = mts.contains(&MeasurementType::LidarScan);
        if !(is_camera || is_lidar) {
            continue;
        }
        if let Some(f) = filter {
            let matches = match f {
                "Camera" => is_camera,
                "Lidar" => is_lidar,
                _ => false,
            };
            if !matches {
                continue;
            }
        }
        out.push(AvailableSensor {
            name: s.name,
            owner: s.owner,
            measurement_types: mts,
        });
    }
    out
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
// Transform helpers
// ---------------------------------------------------------------------------

fn parse_transform(text: &str) -> Result<Transform, String> {
    let parts: Vec<f64> = text
        .split_whitespace()
        .map(|s| s.parse::<f64>())
        .collect::<Result<_, _>>()
        .map_err(|e| format!("Failed to parse number: {}", e))?;
    if parts.len() != 6 {
        return Err("Expected 6 values: X Y Z Roll Pitch Yaw".into());
    }
    Ok(Transform {
        location: Some(Vector {
            x: parts[0],
            y: parts[1],
            z: parts[2],
        }),
        rotation: Some(Rotation {
            r: parts[3] * std::f64::consts::PI / 180.0,
            p: parts[4] * std::f64::consts::PI / 180.0,
            y: parts[5] * std::f64::consts::PI / 180.0,
        }),
    })
}

fn format_transform(t: &Transform) -> String {
    let l = t.location.clone().unwrap_or(Vector { x: 0.0, y: 0.0, z: 0.0 });
    let r = t.rotation.clone().unwrap_or(Rotation { r: 0.0, p: 0.0, y: 0.0 });
    let to_deg = 180.0 / std::f64::consts::PI;
    format!(
        "Location({:.2}, {:.2}, {:.2}) Rotation({:.1}, {:.1}, {:.1})",
        l.x,
        l.y,
        l.z,
        r.r * to_deg,
        r.p * to_deg,
        r.y * to_deg,
    )
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct State {
    streams: HashMap<String, (JoinHandle<()>, Option<WindowProxy>)>,
    recordings: HashMap<String, (JoinHandle<()>, PathBuf)>,
}

// ---------------------------------------------------------------------------
// Sensor helpers
// ---------------------------------------------------------------------------

async fn randomize_camera_post_process(
    camera_name: &str,
    owner: &str,
) -> Result<(), TempoError> {
    let mut rng = rand::thread_rng();
    let scalars: [(&str, &str, f32); 4] = [
        (
            "PostProcessSettings.bOverride_WhiteTemp",
            "PostProcessSettings.WhiteTemp",
            rng.gen_range(700.0_f32..=2000.0_f32),
        ),
        (
            "PostProcessSettings.bOverride_BloomIntensity",
            "PostProcessSettings.BloomIntensity",
            rng.gen_range(0.0_f32..=1.0_f32),
        ),
        (
            "PostProcessSettings.bOverride_AutoExposureSpeedUp",
            "PostProcessSettings.AutoExposureSpeedUp",
            rng.gen_range(1.0_f32..=20.0_f32),
        ),
        (
            "PostProcessSettings.bOverride_AutoExposureSpeedDown",
            "PostProcessSettings.AutoExposureSpeedDown",
            rng.gen_range(1.0_f32..=20.0_f32),
        ),
    ];
    for (override_prop, prop, value) in scalars {
        tempo_world::set_bool_property_async(
            owner.to_string(),
            camera_name.to_string(),
            override_prop.to_string(),
            true,
        )
        .await?;
        tempo_world::set_float_property_async(
            owner.to_string(),
            camera_name.to_string(),
            prop.to_string(),
            value,
        )
        .await?;
    }
    tempo_world::set_bool_property_async(
        owner.to_string(),
        camera_name.to_string(),
        "PostProcessSettings.bOverride_ColorSaturation".to_string(),
        true,
    )
    .await?;
    for axis in ["X", "Y", "Z", "W"] {
        let prop = format!("PostProcessSettings.ColorSaturation.{}", axis);
        let v: f32 = rng.gen_range(0.0_f32..=1.0_f32);
        tempo_world::set_float_property_async(
            owner.to_string(),
            camera_name.to_string(),
            prop,
            v,
        )
        .await?;
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// Streaming tasks
// ---------------------------------------------------------------------------

struct WindowGuard(WindowProxy);

impl Drop for WindowGuard {
    fn drop(&mut self) {
        let _ = self.0.run_function(|w| {
            w.destroy();
        });
    }
}

async fn stream_color(sensor: AvailableSensor, window: WindowProxy) {
    let key = format!("{}:{}:Color", sensor.owner, sensor.name);
    let _guard = WindowGuard(window.clone());
    let mut stream =
        match tempo_sensors::stream_color_images_async(sensor.owner, sensor.name, 0).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start stream: {}", key, e);
                return;
            }
        };
    let mut count = 0u64;
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                count += 1;
                let info = if img.encoding == ColorEncoding::Rgb8 as i32 {
                    ImageInfo::rgb8(img.width, img.height)
                } else {
                    ImageInfo::bgr8(img.width, img.height)
                };
                let view = ImageView::new(info, &img.data);
                if window.set_image("frame", view).is_err() {
                    eprintln!("[{}] Window closed, stopping stream.", key);
                    break;
                }
                if count % 30 == 1 {
                    println!(
                        "[{}] Color frame {} {}x{} ({} bytes)",
                        key,
                        count,
                        img.width,
                        img.height,
                        img.data.len()
                    );
                }
            }
            Err(e) => {
                eprintln!("[{}] Stream error: {}", key, e);
                break;
            }
        }
    }
}

async fn stream_depth(sensor: AvailableSensor, window: WindowProxy) {
    let key = format!("{}:{}:Depth", sensor.owner, sensor.name);
    let _guard = WindowGuard(window.clone());
    let mut stream =
        match tempo_sensors::stream_depth_images_async(sensor.owner, sensor.name).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start stream: {}", key, e);
                return;
            }
        };
    let mut count = 0u64;
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                count += 1;
                let mut mn = f32::INFINITY;
                let mut mx = f32::NEG_INFINITY;
                for &d in &img.depths {
                    if d.is_finite() {
                        if d < mn {
                            mn = d;
                        }
                        if d > mx {
                            mx = d;
                        }
                    }
                }
                if !mn.is_finite() {
                    mn = 0.0;
                    mx = 1.0;
                }
                let span = (mx - mn).max(1e-6);
                let bytes: Vec<u8> = img
                    .depths
                    .iter()
                    .map(|&d| {
                        let n = ((d - mn) / span).clamp(0.0, 1.0);
                        (n * 255.0) as u8
                    })
                    .collect();
                let view = ImageView::new(ImageInfo::mono8(img.width, img.height), &bytes);
                if window.set_image("frame", view).is_err() {
                    eprintln!("[{}] Window closed, stopping stream.", key);
                    break;
                }
                if count % 30 == 1 {
                    println!(
                        "[{}] Depth frame {} {}x{} (min={:.1}cm max={:.1}cm)",
                        key, count, img.width, img.height, mn, mx
                    );
                }
            }
            Err(e) => {
                eprintln!("[{}] Stream error: {}", key, e);
                break;
            }
        }
    }
}

async fn stream_label(sensor: AvailableSensor, window: WindowProxy) {
    let key = format!("{}:{}:Label", sensor.owner, sensor.name);
    let _guard = WindowGuard(window.clone());
    let mut stream =
        match tempo_sensors::stream_label_images_async(sensor.owner, sensor.name).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start stream: {}", key, e);
                return;
            }
        };
    let mut count = 0u64;
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                count += 1;
                let view = ImageView::new(ImageInfo::mono8(img.width, img.height), &img.data);
                if window.set_image("frame", view).is_err() {
                    eprintln!("[{}] Window closed, stopping stream.", key);
                    break;
                }
                if count % 30 == 1 {
                    let unique = img
                        .data
                        .iter()
                        .copied()
                        .collect::<std::collections::BTreeSet<u8>>()
                        .len();
                    println!(
                        "[{}] Label frame {} {}x{} ({} unique labels)",
                        key, count, img.width, img.height, unique
                    );
                }
            }
            Err(e) => {
                eprintln!("[{}] Stream error: {}", key, e);
                break;
            }
        }
    }
}

async fn stream_lidar(sensor: AvailableSensor) {
    let key = format!("{}:{}:PointCloud", sensor.owner, sensor.name);
    let mut stream =
        match tempo_sensors::stream_lidar_scans_async(sensor.owner, sensor.name).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start stream: {}", key, e);
                return;
            }
        };
    let mut count = 0u64;
    while let Some(item) = stream.next().await {
        match item {
            Ok(seg) => {
                count += 1;
                if count % 10 == 1 {
                    println!(
                        "[{}] Lidar segment {} returns={} v_beams={} h_beams={}",
                        key,
                        count,
                        seg.distances.len(),
                        seg.vertical_beams,
                        seg.horizontal_beams
                    );
                }
            }
            Err(e) => {
                eprintln!("[{}] Stream error: {}", key, e);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Recording tasks
// ---------------------------------------------------------------------------

async fn record_color(sensor: AvailableSensor, dir: PathBuf) {
    let key = format!("{}:{}:Color", sensor.owner, sensor.name);
    let mut stream =
        match tempo_sensors::stream_color_images_async(sensor.owner, sensor.name, 0).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start: {}", key, e);
                return;
            }
        };
    let mut count = 0u64;
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                let mut rgb = img.data.clone();
                if img.encoding != ColorEncoding::Rgb8 as i32 {
                    for px in rgb.chunks_exact_mut(3) {
                        px.swap(0, 2);
                    }
                }
                let path = dir.join(format!("frame_{:06}.jpg", count));
                if let Some(buf) =
                    image::RgbImage::from_raw(img.width, img.height, rgb)
                {
                    let _ = buf.save(&path);
                }
                count += 1;
            }
            Err(e) => {
                eprintln!("[{}] Stream error: {}", key, e);
                break;
            }
        }
    }
}

async fn record_depth(sensor: AvailableSensor, dir: PathBuf) {
    let key = format!("{}:{}:Depth", sensor.owner, sensor.name);
    let mut stream =
        match tempo_sensors::stream_depth_images_async(sensor.owner, sensor.name).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start: {}", key, e);
                return;
            }
        };
    let mut count = 0u64;
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                let path = dir.join(format!("frame_{:06}_{}x{}.f32", count, img.width, img.height));
                if let Ok(mut f) = fs::File::create(&path) {
                    let bytes: Vec<u8> = img.depths.iter().flat_map(|v| v.to_le_bytes()).collect();
                    let _ = f.write_all(&bytes);
                }
                count += 1;
            }
            Err(e) => {
                eprintln!("[{}] Stream error: {}", key, e);
                break;
            }
        }
    }
}

async fn record_label(sensor: AvailableSensor, dir: PathBuf) {
    let key = format!("{}:{}:Label", sensor.owner, sensor.name);
    let mut stream =
        match tempo_sensors::stream_label_images_async(sensor.owner, sensor.name).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start: {}", key, e);
                return;
            }
        };
    let mut count = 0u64;
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                let path = dir.join(format!("frame_{:06}.pgm", count));
                if let Ok(mut f) = fs::File::create(&path) {
                    let header = format!("P5\n{} {}\n255\n", img.width, img.height);
                    let _ = f.write_all(header.as_bytes());
                    let _ = f.write_all(&img.data);
                }
                count += 1;
            }
            Err(e) => {
                eprintln!("[{}] Stream error: {}", key, e);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Flows
// ---------------------------------------------------------------------------

async fn flow_add_sensor() {
    println!("\n--- Add Sensor ---");
    let Some(sensor_type) = text_input("What type of sensor?", "TempoCamera").await else {
        return;
    };
    let Some(actor) = text_input("Owner actor?", "BP_SensorRig").await else {
        return;
    };
    let Some(parent) = text_input("Parent component (empty for root)?", "").await else {
        return;
    };
    let Some(socket) = text_input("Socket (empty for none)?", "").await else {
        return;
    };
    let transform = Transform {
        location: Some(Vector { x: 0.0, y: 0.0, z: 0.0 }),
        rotation: Some(Rotation { r: 0.0, p: 0.0, y: 0.0 }),
    };
    match tempo_world::add_component_async(
        sensor_type,
        actor,
        String::new(),
        parent,
        transform,
        socket,
    )
    .await
    {
        Ok(resp) => {
            println!("\n  Added component: {}", resp.name);
            if let Some(t) = &resp.transform {
                println!("  Transform: {}", format_transform(t));
            }
        }
        Err(e) => println!("  Error while adding component: {}", e),
    }
}

async fn flow_remove_sensor() {
    println!("\n--- Remove Sensor ---");
    let sensors = get_available_sensors(None).await;
    if sensors.is_empty() {
        println!("  No sensors found.");
        return;
    }
    let labels: Vec<String> = sensors.iter().map(|s| s.label()).collect();
    let Some(chosen) = select_one("Which sensor?", labels).await else {
        return;
    };
    let Some(s) = sensors.into_iter().find(|s| s.label() == chosen) else {
        return;
    };
    match tempo_world::destroy_component_async(s.owner.clone(), s.name.clone()).await {
        Ok(_) => println!("\n  Destroyed: {}", s.label()),
        Err(e) => println!("  Error while destroying component: {}", e),
    }
}

async fn flow_reposition_sensor() {
    println!("\n--- Reposition Sensor ---");
    let sensors = get_available_sensors(None).await;
    if sensors.is_empty() {
        println!("  No sensors found.");
        return;
    }
    let labels: Vec<String> = sensors.iter().map(|s| s.label()).collect();
    let Some(chosen) = select_one("Which sensor?", labels).await else {
        return;
    };
    let Some(s) = sensors.into_iter().find(|x| x.label() == chosen) else {
        return;
    };
    let Some(text) = text_input("New transform (X Y Z R P Y, Meters/Degrees)", "0 0 0 0 0 0").await
    else {
        return;
    };
    let t = match parse_transform(&text) {
        Ok(t) => t,
        Err(e) => {
            println!("  {}", e);
            return;
        }
    };
    match tempo_world::set_component_transform_async(s.owner, s.name, t, false).await {
        Ok(_) => println!("\n  Repositioned {}", chosen),
        Err(e) => println!("  Error while repositioning component: {}", e),
    }
}

async fn flow_get_sensor_properties() {
    println!("\n--- Get Sensor Properties ---");
    let sensors = get_available_sensors(None).await;
    if sensors.is_empty() {
        println!("  No sensors found.");
        return;
    }
    let labels: Vec<String> = sensors.iter().map(|s| s.label()).collect();
    let Some(chosen) = select_one("Which sensor?", labels).await else {
        return;
    };
    let Some(s) = sensors.into_iter().find(|x| x.label() == chosen) else {
        return;
    };
    match tempo_world::get_component_properties_async(s.owner, s.name).await {
        Ok(resp) => {
            println!();
            for p in resp.properties {
                if p.property_type != "unsupported" {
                    println!("  {}({}): {}", p.name, p.property_type, p.value);
                }
            }
        }
        Err(e) => println!("  Error while getting sensor properties: {}", e),
    }
}

async fn flow_randomize_post_process() {
    println!("\n--- Randomize Camera Post-Process ---");
    let cameras = get_available_sensors(Some("Camera")).await;
    if cameras.is_empty() {
        println!("  No cameras found.");
        return;
    }
    let labels: Vec<String> = cameras.iter().map(|s| s.label()).collect();
    let Some(chosen) = select_one("Which camera?", labels).await else {
        return;
    };
    let Some(c) = cameras.into_iter().find(|x| x.label() == chosen) else {
        return;
    };
    match randomize_camera_post_process(&c.name, &c.owner).await {
        Ok(_) => println!("\n  Randomized post-process on {}", chosen),
        Err(e) => println!("  Error while setting sensor properties: {}", e),
    }
}

async fn flow_start_stream(state: &mut State) {
    println!("\n--- Start Sensor Data Stream ---");
    let sensors = get_available_sensors(None).await;
    if sensors.is_empty() {
        println!("  No sensors found.");
        return;
    }
    let mut entries: Vec<(AvailableSensor, MeasurementType, String)> = Vec::new();
    for s in &sensors {
        for &mt in &s.measurement_types {
            let label = format!("{}:{}:{}", s.owner, s.name, measurement_type_label(mt));
            entries.push((s.clone(), mt, label));
        }
    }
    if entries.is_empty() {
        println!("  No sensor streams available.");
        return;
    }
    let labels: Vec<String> = entries.iter().map(|(_, _, l)| l.clone()).collect();
    let Some(chosen) = select_one("Which sensor stream?", labels).await else {
        return;
    };
    let Some((sensor, mt, key)) = entries.into_iter().find(|(_, _, l)| l == &chosen) else {
        return;
    };
    if let Some((handle, window)) = state.streams.remove(&key) {
        println!("\n  Restarting stream {}", key);
        handle.abort();
        if let Some(w) = window {
            let _ = w.run_function(|w| {
                w.destroy();
            });
        }
    }
    let needs_window = matches!(
        mt,
        MeasurementType::ColorImage | MeasurementType::DepthImage | MeasurementType::LabelImage
    );
    let window = if needs_window {
        match show_image::create_window(&key, Default::default()) {
            Ok(w) => Some(w),
            Err(e) => {
                println!("  Failed to create window: {}", e);
                return;
            }
        }
    } else {
        None
    };
    let handle = match (mt, window.clone()) {
        (MeasurementType::ColorImage, Some(w)) => tokio::spawn(stream_color(sensor, w)),
        (MeasurementType::DepthImage, Some(w)) => tokio::spawn(stream_depth(sensor, w)),
        (MeasurementType::LabelImage, Some(w)) => tokio::spawn(stream_label(sensor, w)),
        (MeasurementType::LidarScan, _) => tokio::spawn(stream_lidar(sensor)),
        (MeasurementType::BoundingBoxes, _) => {
            println!("  BoundingBoxes streaming not implemented in this client.");
            return;
        }
        _ => return,
    };
    state.streams.insert(key.clone(), (handle, window));
    println!("\n  Started stream: {}", key);
}

async fn flow_end_stream(state: &mut State) {
    println!("\n--- End Sensor Data Stream ---");
    state.streams.retain(|_, (h, _)| !h.is_finished());
    if state.streams.is_empty() {
        println!("  No active streams.");
        return;
    }
    let labels: Vec<String> = state.streams.keys().cloned().collect();
    let Some(chosen) = select_one("Which stream?", labels).await else {
        return;
    };
    if let Some((h, window)) = state.streams.remove(&chosen) {
        h.abort();
        if let Some(w) = window {
            let _ = w.run_function(|w| {
                w.destroy();
            });
        }
        println!("\n  Ended stream: {}", chosen);
    }
}

async fn flow_start_recording(state: &mut State) {
    println!("\n--- Start Sensor Data Recording ---");
    let cameras = get_available_sensors(Some("Camera")).await;
    if cameras.is_empty() {
        println!("  No cameras found.");
        return;
    }
    let mut entries: Vec<(AvailableSensor, MeasurementType, String)> = Vec::new();
    for s in &cameras {
        for &mt in &s.measurement_types {
            if mt == MeasurementType::LidarScan {
                continue;
            }
            let label = format!("{}:{}:{}", s.owner, s.name, measurement_type_label(mt));
            entries.push((s.clone(), mt, label));
        }
    }
    if entries.is_empty() {
        println!("  No camera streams available.");
        return;
    }
    let labels: Vec<String> = entries.iter().map(|(_, _, l)| l.clone()).collect();
    let Some(chosen) = select_one("Which sensor stream?", labels).await else {
        return;
    };
    let Some((sensor, mt, key)) = entries.into_iter().find(|(_, _, l)| l == &chosen) else {
        return;
    };
    if state.recordings.contains_key(&key) {
        println!("\n  Already recording {}", key);
        return;
    }
    let dir = match tempfile::Builder::new()
        .prefix(&format!(
            "tempo_recording_{}_{}_",
            sensor.name,
            measurement_type_label(mt)
        ))
        .tempdir()
    {
        Ok(d) => d.keep(),
        Err(e) => {
            println!("  Failed to create temp dir: {}", e);
            return;
        }
    };
    println!("\n  Recording {} to: {}", key, dir.display());
    let handle = match mt {
        MeasurementType::ColorImage => tokio::spawn(record_color(sensor, dir.clone())),
        MeasurementType::DepthImage => tokio::spawn(record_depth(sensor, dir.clone())),
        MeasurementType::LabelImage => tokio::spawn(record_label(sensor, dir.clone())),
        _ => {
            println!("  Unsupported measurement type for recording.");
            return;
        }
    };
    state.recordings.insert(key, (handle, dir));
}

async fn flow_end_recording(state: &mut State) {
    println!("\n--- End Sensor Data Recording ---");
    if state.recordings.is_empty() {
        println!("  No active recordings.");
        return;
    }
    let labels: Vec<String> = state.recordings.keys().cloned().collect();
    let Some(chosen) = select_one("Which recording?", labels).await else {
        return;
    };
    if let Some((h, dir)) = state.recordings.remove(&chosen) {
        h.abort();
        println!("\n  Ended recording: {}", chosen);
        println!("  Files saved to: {}", dir.display());
    }
}

async fn flow_move_actor() {
    println!("\n--- Move Actor ---");
    let Some(actor) = text_input("Which actor?", "BP_SensorRig").await else {
        return;
    };
    let Some(text) =
        text_input("Relative transform (X Y Z R P Y, Meters/Degrees)", "0 0 0 0 0 0").await
    else {
        return;
    };
    let t = match parse_transform(&text) {
        Ok(t) => t,
        Err(e) => {
            println!("  {}", e);
            return;
        }
    };
    match tempo_world::set_actor_transform_async(actor.clone(), t, String::new()).await {
        Ok(_) => println!("\n  Moved {}", actor),
        Err(e) => println!("  Error while moving actor: {}", e),
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

#[show_image::main]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    runtime.block_on(async_main());
    Ok(())
}

async fn async_main() {
    let args = Args::parse();
    if args.ip != "0.0.0.0" || args.port != 10001 {
        set_server_async(&args.ip, args.port).await;
    }

    let mut state = State {
        streams: HashMap::new(),
        recordings: HashMap::new(),
    };

    println!("\n=== Sensor Playground ===");
    println!("Add, remove, reposition, stream, and record sensors at runtime.\n");

    let actions: Vec<&str> = vec![
        "Add a sensor",
        "Remove a sensor",
        "Reposition a sensor",
        "Get a sensor's properties",
        "Randomize a camera's post-process properties",
        "Start sensor data stream",
        "End sensor data stream",
        "Start sensor data recording",
        "End sensor data recording",
        "Move a sensor's owner Actor",
        "Quit",
    ];

    loop {
        let options: Vec<String> = actions.iter().map(|s| s.to_string()).collect();
        let Some(action) = select_one("What would you like to do?", options).await else {
            break;
        };
        match action.as_str() {
            "Add a sensor" => flow_add_sensor().await,
            "Remove a sensor" => flow_remove_sensor().await,
            "Reposition a sensor" => flow_reposition_sensor().await,
            "Get a sensor's properties" => flow_get_sensor_properties().await,
            "Randomize a camera's post-process properties" => flow_randomize_post_process().await,
            "Start sensor data stream" => flow_start_stream(&mut state).await,
            "End sensor data stream" => flow_end_stream(&mut state).await,
            "Start sensor data recording" => flow_start_recording(&mut state).await,
            "End sensor data recording" => flow_end_recording(&mut state).await,
            "Move a sensor's owner Actor" => flow_move_actor().await,
            "Quit" => break,
            _ => {}
        }
    }

    for (_, (h, window)) in state.streams.drain() {
        h.abort();
        if let Some(w) = window {
            let _ = w.run_function(|w| {
                w.destroy();
            });
        }
    }
    for (k, (h, dir)) in state.recordings.drain() {
        h.abort();
        println!("  Ended recording {}. Files saved to: {}", k, dir.display());
    }
    println!("\nBye!\n");
}
