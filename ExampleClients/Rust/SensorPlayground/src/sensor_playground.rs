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
use tempo_sim::proto::tempo_sensors::ColorEncoding;
use tempo_sim::proto::tempo_core::{Rotation, Transform, Vector};
use tempo_sim::proto::tempo_sensors::MeasurementType;
use tempo_sim::{
    default_unix_socket_path, set_server_async, set_unix_socket_async, tempo_sensors, tempo_world,
    TempoError,
};
use tokio::task::JoinHandle;
use tokio_stream::StreamExt;

#[derive(Parser, Debug)]
struct Args {
    #[arg(long, help = "IP address of machine where Tempo is running. If unset, connect via Unix domain socket.")]
    ip: Option<String>,
    #[arg(long, default_value_t = 10001u16, help = "Port Tempo gRPC server is using")]
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
        MeasurementType::MtUnknown => "Unknown",
        MeasurementType::MtColorImage => "Color",
        MeasurementType::MtDepthImage => "Depth",
        MeasurementType::MtLabelImage => "Label",
        MeasurementType::MtLidarScan => "PointCloud",
        MeasurementType::MtBoundingBoxes => "BoundingBoxes",
        MeasurementType::MtVideo => "Video",
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
        let is_camera = mts.contains(&MeasurementType::MtColorImage);
        let is_lidar = mts.contains(&MeasurementType::MtLidarScan);
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
        match tempo_sensors::stream_color_images_async(sensor.owner, sensor.name).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start stream: {}", key, e);
                return;
            }
        };
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                let info = if img.encoding == ColorEncoding::CeRgb8 as i32 {
                    ImageInfo::rgb8(img.width_px, img.height_px)
                } else {
                    ImageInfo::bgr8(img.width_px, img.height_px)
                };
                let view = ImageView::new(info, &img.data);
                if window.set_image("frame", view).is_err() {
                    eprintln!("[{}] Window closed, stopping stream.", key);
                    break;
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
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                // Visualize 1/depth so near surfaces have more contrast — matches
                // _build_depth_qimage in TempoImageUtils.py.
                let mut recip_mn = f32::INFINITY;
                let mut recip_mx = f32::NEG_INFINITY;
                for &d in &img.depths_m {
                    let r = 1.0 / d;
                    if r.is_finite() {
                        if r < recip_mn {
                            recip_mn = r;
                        }
                        if r > recip_mx {
                            recip_mx = r;
                        }
                    }
                }
                if !recip_mn.is_finite() {
                    recip_mn = 0.0;
                    recip_mx = 1.0;
                }
                let span = (recip_mx - recip_mn).max(1e-6);
                let bytes: Vec<u8> = img
                    .depths_m
                    .iter()
                    .map(|&d| {
                        let r = 1.0 / d;
                        let n = if r.is_finite() {
                            ((r - recip_mn) / span).clamp(0.0, 1.0)
                        } else {
                            0.0
                        };
                        (n * 255.0) as u8
                    })
                    .collect();
                let view = ImageView::new(ImageInfo::mono8(img.width_px, img.height_px), &bytes);
                if window.set_image("frame", view).is_err() {
                    eprintln!("[{}] Window closed, stopping stream.", key);
                    break;
                }
            }
            Err(e) => {
                eprintln!("[{}] Stream error: {}", key, e);
                break;
            }
        }
    }
}

// Golden-ratio HSV → RGB lookup table for label visualization. Matches
// index_to_rgb in TempoImageUtils.py so the same label IDs render in the same
// colors across the Python and Rust clients.
fn label_lut() -> &'static [[u8; 3]; 256] {
    static LUT: std::sync::OnceLock<[[u8; 3]; 256]> = std::sync::OnceLock::new();
    LUT.get_or_init(|| {
        let mut lut = [[0u8; 3]; 256];
        for i in 1..256 {
            let i_f = i as f64;
            let phi = 0.618_033_988_749_895_f64;
            let psi = 0.754_877_666_246_692_7_f64;
            let h = (i_f * phi).rem_euclid(1.0);
            let s = 0.6 + 0.4 * (i_f * psi).rem_euclid(1.0);
            let v = 0.7 + 0.3 * (i_f * psi * 1.3).rem_euclid(1.0);
            let (r, g, b) = hsv_to_rgb(h, s, v);
            lut[i] = [(r * 255.0) as u8, (g * 255.0) as u8, (b * 255.0) as u8];
        }
        lut
    })
}

fn hsv_to_rgb(h: f64, s: f64, v: f64) -> (f64, f64, f64) {
    if s == 0.0 {
        return (v, v, v);
    }
    let sector = h * 6.0;
    let i = sector.floor();
    let f = sector - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));
    match (i as i32).rem_euclid(6) {
        0 => (v, t, p),
        1 => (q, v, p),
        2 => (p, v, t),
        3 => (p, q, v),
        4 => (t, p, v),
        _ => (v, p, q),
    }
}

async fn stream_video(sensor: AvailableSensor, window: WindowProxy) {
    let key = format!("{}:{}:Video", sensor.owner, sensor.name);
    let _guard = WindowGuard(window.clone());

    // Initialize libavcodec once. Subsequent calls are cheap.
    if let Err(e) = ffmpeg_next::init() {
        eprintln!("[{}] ffmpeg init failed: {}", key, e);
        return;
    }
    // Suppress libswscale's per-frame "No accelerated colorspace conversion found from yuv420p to
    // rgb24" warning. macOS arm64 has no SIMD path for that pair, so swscale always falls back to
    // C — functionally fine, just noisy.
    ffmpeg_next::util::log::set_level(ffmpeg_next::util::log::Level::Error);

    let mut stream = match tempo_sensors::stream_video_async(
        sensor.owner.clone(),
        sensor.name.clone(),
        0, // codec H264
        0, // default bitrate
        0, // default KFI
        0, // default profile
    ).await {
        Ok(s) => s,
        Err(e) => {
            eprintln!("[{}] Failed to start stream: {}", key, e);
            return;
        }
    };

    // Build the decoder lazily after we see the first frame's width/height — sws_scale needs the
    // input format declared at construction.
    let codec = match ffmpeg_next::codec::decoder::find(ffmpeg_next::codec::Id::H264) {
        Some(c) => c,
        None => {
            eprintln!("[{}] H264 decoder not available in this libavcodec build.", key);
            return;
        }
    };
    let codec_ctx = match ffmpeg_next::codec::Context::new_with_codec(codec).decoder().open_as(codec) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("[{}] Failed to open H264 decoder: {}", key, e);
            return;
        }
    };
    let mut decoder = codec_ctx.video().expect("H264 decoder is a video decoder");

    let mut seen_key = false;

    while let Some(item) = stream.next().await {
        match item {
            Ok(frame) => {
                if !seen_key {
                    if !frame.key_frame {
                        continue;
                    }
                    seen_key = true;
                }
                let mut packet = ffmpeg_next::packet::Packet::copy(&frame.data);
                if frame.key_frame {
                    packet.set_flags(ffmpeg_next::packet::Flags::KEY);
                }
                if let Err(e) = decoder.send_packet(&packet) {
                    eprintln!("[{}] decoder.send_packet error: {}; resyncing on next keyframe.", key, e);
                    seen_key = false;
                    continue;
                }
                // Drain decoded frames inside a sync block so the SwsContext (which is *mut C
                // state and therefore not Send) is dropped before the next stream.next().await —
                // tokio's spawn requires the future to be Send.
                let mut decoded = ffmpeg_next::frame::Video::empty();
                let mut rgb_frame = ffmpeg_next::frame::Video::empty();
                let display_result = (|| -> Result<(), String> {
                    while decoder.receive_frame(&mut decoded).is_ok() {
                        let w = decoded.width();
                        let h = decoded.height();
                        let in_format = decoded.format();
                        let mut scaler = ffmpeg_next::software::scaling::Context::get(
                            in_format,
                            w,
                            h,
                            ffmpeg_next::format::Pixel::RGB24,
                            w,
                            h,
                            ffmpeg_next::software::scaling::Flags::BILINEAR,
                        ).map_err(|e| format!("sws_scale ctx: {}", e))?;
                        scaler.run(&decoded, &mut rgb_frame).map_err(|e| format!("sws_scale run: {}", e))?;
                        let view = ImageView::new(ImageInfo::rgb8(w, h), rgb_frame.data(0));
                        if window.set_image("frame", view).is_err() {
                            return Err("window closed".into());
                        }
                    }
                    Ok(())
                })();
                if let Err(e) = display_result {
                    if e == "window closed" {
                        eprintln!("[{}] Window closed, stopping stream.", key);
                        return;
                    }
                    eprintln!("[{}] {}", key, e);
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
    let lut = label_lut();
    while let Some(item) = stream.next().await {
        match item {
            Ok(img) => {
                let mut rgb = Vec::with_capacity(img.data.len() * 3);
                for &idx in &img.data {
                    let c = lut[idx as usize];
                    rgb.extend_from_slice(&c);
                }
                let view = ImageView::new(ImageInfo::rgb8(img.width_px, img.height_px), &rgb);
                if window.set_image("frame", view).is_err() {
                    eprintln!("[{}] Window closed, stopping stream.", key);
                    break;
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
// Lidar 3D viewer
// ---------------------------------------------------------------------------

#[cfg_attr(target_os = "macos", allow(dead_code))]
struct PointCloud {
    points: Vec<[f32; 3]>,
    colors: Vec<[f32; 3]>,
}

#[cfg_attr(target_os = "macos", allow(dead_code))]
enum LidarMsg {
    Update(String, PointCloud),
    Remove(String),
}

#[cfg(target_os = "macos")]
fn lidar_viewer_sender() -> Option<&'static std::sync::mpsc::Sender<LidarMsg>> {
    None
}

#[cfg(not(target_os = "macos"))]
fn lidar_viewer_sender() -> Option<&'static std::sync::mpsc::Sender<LidarMsg>> {
    static LIDAR_VIEWER: std::sync::OnceLock<std::sync::mpsc::Sender<LidarMsg>> =
        std::sync::OnceLock::new();
    Some(LIDAR_VIEWER.get_or_init(|| {
        let (tx, rx) = std::sync::mpsc::channel();
        std::thread::spawn(move || run_lidar_viewer(rx));
        tx
    }))
}

#[cfg(not(target_os = "macos"))]
fn run_lidar_viewer(rx: std::sync::mpsc::Receiver<LidarMsg>) {
    use kiss3d::camera::ArcBall;
    use kiss3d::nalgebra::{Point3, Vector3};
    use kiss3d::window::Window;

    let mut window = Window::new("Tempo Lidar");
    window.set_background_color(0.0, 0.0, 0.0);
    window.set_point_size(3.0);

    let mut camera = ArcBall::new(Point3::new(-10.0, 0.0, 5.0), Point3::origin());
    camera.set_up_axis(Vector3::z());

    let mut clouds: HashMap<String, PointCloud> = HashMap::new();
    let origin = Point3::origin();
    let red = Point3::new(1.0, 0.0, 0.0);
    let green = Point3::new(0.0, 1.0, 0.0);
    let blue = Point3::new(0.0, 0.0, 1.0);

    while window.render_with_camera(&mut camera) {
        loop {
            match rx.try_recv() {
                Ok(LidarMsg::Update(k, pc)) => {
                    clouds.insert(k, pc);
                }
                Ok(LidarMsg::Remove(k)) => {
                    clouds.remove(&k);
                }
                Err(std::sync::mpsc::TryRecvError::Empty) => break,
                Err(std::sync::mpsc::TryRecvError::Disconnected) => return,
            }
        }
        window.draw_line(&origin, &Point3::new(1.0, 0.0, 0.0), &red);
        window.draw_line(&origin, &Point3::new(0.0, 1.0, 0.0), &green);
        window.draw_line(&origin, &Point3::new(0.0, 0.0, 1.0), &blue);
        for cloud in clouds.values() {
            for (p, c) in cloud.points.iter().zip(cloud.colors.iter()) {
                window.draw_point(
                    &Point3::new(p[0], p[1], p[2]),
                    &Point3::new(c[0], c[1], c[2]),
                );
            }
        }
    }
}

// 5-stop linear approximation of matplotlib viridis. Good enough to give the
// point cloud a continuous color gradient; full 256-entry LUT would be overkill.
fn viridis(t: f32) -> [f32; 3] {
    const STOPS: [[f32; 3]; 5] = [
        [0.267, 0.005, 0.329],
        [0.231, 0.318, 0.545],
        [0.128, 0.567, 0.551],
        [0.369, 0.789, 0.382],
        [0.993, 0.906, 0.144],
    ];
    let t = t.clamp(0.0, 1.0);
    let scaled = t * 4.0;
    let i = scaled.floor() as usize;
    if i >= 4 {
        return STOPS[4];
    }
    let f = scaled - i as f32;
    let a = STOPS[i];
    let b = STOPS[i + 1];
    [
        a[0] + (b[0] - a[0]) * f,
        a[1] + (b[1] - a[1]) * f,
        a[2] + (b[2] - a[2]) * f,
    ]
}

#[derive(Default)]
struct LidarAccumulator {
    sequence_id: Option<u64>,
    expected_segments: i32,
    received_segments: i32,
    distances: Vec<f32>,
    intensities: Vec<f32>,
    azimuths: Vec<f32>,
    elevations: Vec<f32>,
}

impl LidarAccumulator {
    fn add(&mut self, seg: &tempo_sim::proto::tempo_sensors::LidarScanSegment) -> bool {
        let id = seg.header.as_ref().map(|h| h.sequence_id);
        let restart = match (self.sequence_id, id) {
            (Some(a), Some(b)) => a != b,
            _ => true,
        };
        if restart {
            self.distances.clear();
            self.intensities.clear();
            self.azimuths.clear();
            self.elevations.clear();
            self.received_segments = 0;
            self.sequence_id = id;
            self.expected_segments = seg.scan_count;
        }
        self.distances.extend_from_slice(&seg.distances_m);
        self.intensities.extend_from_slice(&seg.intensities);
        self.azimuths.extend_from_slice(&seg.azimuths_rad);
        self.elevations.extend_from_slice(&seg.elevations_rad);
        self.received_segments += 1;
        self.expected_segments > 0 && self.received_segments == self.expected_segments
    }

    fn build_cloud(&self) -> PointCloud {
        let n = self.distances.len();
        let mut points = Vec::with_capacity(n);
        let mut colors = Vec::with_capacity(n);
        let mut mn = f32::INFINITY;
        let mut mx = f32::NEG_INFINITY;
        for &i in &self.intensities {
            if i.is_finite() {
                if i < mn {
                    mn = i;
                }
                if i > mx {
                    mx = i;
                }
            }
        }
        if !mn.is_finite() {
            mn = 0.0;
            mx = 1.0;
        }
        let span = (mx - mn).max(1e-6);
        for i in 0..n {
            let d = self.distances[i];
            if !d.is_finite() || d <= 0.0 {
                continue;
            }
            let az = self.azimuths[i];
            let el = self.elevations[i];
            let c_el = el.cos();
            let x = d * c_el * az.cos();
            let y = d * c_el * az.sin();
            let z = d * el.sin();
            let t = ((self.intensities[i] - mn) / span).clamp(0.0, 1.0);
            points.push([x, y, z]);
            colors.push(viridis(t));
        }
        PointCloud { points, colors }
    }
}

async fn stream_lidar(sensor: AvailableSensor) {
    let key = format!("{}:{}:PointCloud", sensor.owner, sensor.name);
    let viewer = lidar_viewer_sender();

    struct ViewerGuard {
        key: String,
        viewer: Option<&'static std::sync::mpsc::Sender<LidarMsg>>,
    }
    impl Drop for ViewerGuard {
        fn drop(&mut self) {
            if let Some(s) = self.viewer {
                let _ = s.send(LidarMsg::Remove(self.key.clone()));
            }
        }
    }
    let _guard = ViewerGuard {
        key: key.clone(),
        viewer,
    };

    let mut stream =
        match tempo_sensors::stream_lidar_scans_async(sensor.owner, sensor.name).await {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[{}] Failed to start stream: {}", key, e);
                return;
            }
        };
    let mut accumulator = LidarAccumulator::default();
    while let Some(item) = stream.next().await {
        match item {
            Ok(seg) => {
                if accumulator.add(&seg) {
                    if let Some(s) = viewer {
                        let cloud = accumulator.build_cloud();
                        let _ = s.send(LidarMsg::Update(key.clone(), cloud));
                    }
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
        match tempo_sensors::stream_color_images_async(sensor.owner, sensor.name).await {
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
                if img.encoding != ColorEncoding::CeRgb8 as i32 {
                    for px in rgb.chunks_exact_mut(3) {
                        px.swap(0, 2);
                    }
                }
                let path = dir.join(format!("frame_{:06}.jpg", count));
                if let Some(buf) =
                    image::RgbImage::from_raw(img.width_px, img.height_px, rgb)
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
                let path = dir.join(format!("frame_{:06}_{}x{}.f32", count, img.width_px, img.height_px));
                if let Ok(mut f) = fs::File::create(&path) {
                    let bytes: Vec<u8> = img.depths_m.iter().flat_map(|v| v.to_le_bytes()).collect();
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
                    let header = format!("P5\n{} {}\n255\n", img.width_px, img.height_px);
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
        MeasurementType::MtColorImage | MeasurementType::MtDepthImage | MeasurementType::MtLabelImage | MeasurementType::MtVideo
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
        (MeasurementType::MtColorImage, Some(w)) => tokio::spawn(stream_color(sensor, w)),
        (MeasurementType::MtDepthImage, Some(w)) => tokio::spawn(stream_depth(sensor, w)),
        (MeasurementType::MtLabelImage, Some(w)) => tokio::spawn(stream_label(sensor, w)),
        (MeasurementType::MtVideo, Some(w)) => tokio::spawn(stream_video(sensor, w)),
        (MeasurementType::MtLidarScan, _) => tokio::spawn(stream_lidar(sensor)),
        (MeasurementType::MtBoundingBoxes, _) => {
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
            if mt == MeasurementType::MtLidarScan {
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
        MeasurementType::MtColorImage => tokio::spawn(record_color(sensor, dir.clone())),
        MeasurementType::MtDepthImage => tokio::spawn(record_depth(sensor, dir.clone())),
        MeasurementType::MtLabelImage => tokio::spawn(record_label(sensor, dir.clone())),
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
    match args.ip {
        Some(ip) => set_server_async(&ip, args.port).await,
        None => {
            if args.port != 10001 {
                set_unix_socket_async(default_unix_socket_path(args.port)).await;
            }
        }
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
