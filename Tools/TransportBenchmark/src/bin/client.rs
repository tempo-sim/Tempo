// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Echo client that benchmarks gRPC transport overhead — TCP vs UDS.
//!
//! Two modes:
//!   - `latency`    — N unary calls with a small payload. Reports p50/p95/p99/mean RTT.
//!   - `throughput` — one server-streaming call of N messages at a chosen payload size.
//!                    Reports MB/s and msg/s.
//!
//! By default runs both transports back-to-back against the same `tempo-bench-server` and
//! prints a comparison table. Pass `--transport tcp` or `--transport uds` to run just one.

use std::path::PathBuf;
use std::time::{Duration, Instant};

use clap::{Parser, ValueEnum};
use hyper_util::rt::TokioIo;
use tokio::net::UnixStream;
use tokio_stream::StreamExt;
use tonic::transport::{Channel, Endpoint as TonicEndpoint, Uri};
use tower::service_fn;

use tempo_transport_benchmark::default_socket_path;
use tempo_transport_benchmark::proto::echo_client::EchoClient;
use tempo_transport_benchmark::proto::{EchoRequest, EchoStreamRequest};

#[derive(Clone, Copy, Debug, ValueEnum)]
enum Transport {
    Tcp,
    Uds,
    Both,
}

#[derive(Clone, Copy, Debug, ValueEnum)]
enum Mode {
    Latency,
    Throughput,
    Both,
}

#[derive(Parser, Debug)]
#[command(about = "Tempo transport benchmark: echo client (TCP vs UDS).")]
struct Args {
    /// Which transport(s) to exercise.
    #[arg(long, value_enum, default_value_t = Transport::Both)]
    transport: Transport,

    /// Which mode(s) to run.
    #[arg(long, value_enum, default_value_t = Mode::Both)]
    mode: Mode,

    /// Server host (for TCP). Use the same host as the running tempo-bench-server.
    #[arg(long, default_value = "127.0.0.1")]
    host: String,

    /// TCP port (for TCP).
    #[arg(long, default_value_t = 50051)]
    port: u16,

    /// UDS socket path (for UDS). Defaults to tempo-bench-<port>.sock under the system tmp dir.
    #[arg(long)]
    socket_path: Option<PathBuf>,

    /// Latency mode: number of unary calls.
    #[arg(long, default_value_t = 1000)]
    latency_iters: u32,

    /// Latency mode: payload bytes per call.
    #[arg(long, default_value_t = 16)]
    latency_payload_bytes: u32,

    /// Throughput mode: payload bytes per streamed message.
    #[arg(long, default_value_t = 1_048_576)]
    throughput_payload_bytes: u32,

    /// Throughput mode: number of messages in the streaming response.
    #[arg(long, default_value_t = 200)]
    throughput_count: u32,
}

#[derive(Debug, Clone, Copy)]
#[allow(dead_code)]  // `iters` is captured for the per-leg log line above.
struct LatencyStats {
    iters: u32,
    p50_us: f64,
    p95_us: f64,
    p99_us: f64,
    mean_us: f64,
}

#[derive(Debug, Clone, Copy)]
#[allow(dead_code)]  // `msgs`/`bytes` are captured for the per-leg log line above.
struct ThroughputStats {
    msgs: u64,
    bytes: u64,
    elapsed_s: f64,
    mbps: f64,
    msgs_per_s: f64,
}

async fn dial_tcp(host: &str, port: u16) -> Result<Channel, Box<dyn std::error::Error>> {
    let uri = format!("http://{}:{}", host, port);
    let channel = TonicEndpoint::from_shared(uri)?.connect().await?;
    Ok(channel)
}

#[cfg(unix)]
async fn dial_uds(path: PathBuf) -> Result<Channel, Box<dyn std::error::Error>> {
    let channel = TonicEndpoint::from_static("http://[::]:0")
        .connect_with_connector(service_fn(move |_: Uri| {
            let p = path.clone();
            async move {
                let stream = UnixStream::connect(&p).await?;
                Ok::<_, std::io::Error>(TokioIo::new(stream))
            }
        }))
        .await?;
    Ok(channel)
}

#[cfg(not(unix))]
async fn dial_uds(_path: PathBuf) -> Result<Channel, Box<dyn std::error::Error>> {
    Err("UDS unsupported on this platform".into())
}

async fn run_latency(
    label: &str,
    channel: Channel,
    iters: u32,
    payload_bytes: u32,
) -> Result<LatencyStats, Box<dyn std::error::Error>> {
    let mut client = EchoClient::new(channel);
    let payload = vec![0u8; payload_bytes as usize];

    // Warm-up: a few calls before measuring so the channel and any lazy gRPC state stabilize.
    for _ in 0..10 {
        let _ = client
            .echo(EchoRequest { payload: payload.clone() })
            .await?;
    }

    let mut samples: Vec<Duration> = Vec::with_capacity(iters as usize);
    for _ in 0..iters {
        let start = Instant::now();
        let _ = client
            .echo(EchoRequest { payload: payload.clone() })
            .await?;
        samples.push(start.elapsed());
    }
    samples.sort();
    let p = |q: f64| samples[((iters as f64 * q) as usize).min(iters as usize - 1)];
    let mean = samples.iter().map(|d| d.as_secs_f64()).sum::<f64>() / iters as f64;
    let stats = LatencyStats {
        iters,
        p50_us: p(0.50).as_secs_f64() * 1e6,
        p95_us: p(0.95).as_secs_f64() * 1e6,
        p99_us: p(0.99).as_secs_f64() * 1e6,
        mean_us: mean * 1e6,
    };
    println!(
        "[latency {}] iters={} payload={}B  p50={:.1}us p95={:.1}us p99={:.1}us mean={:.1}us",
        label, iters, payload_bytes, stats.p50_us, stats.p95_us, stats.p99_us, stats.mean_us,
    );
    Ok(stats)
}

async fn run_throughput(
    label: &str,
    channel: Channel,
    payload_bytes: u32,
    count: u32,
) -> Result<ThroughputStats, Box<dyn std::error::Error>> {
    let mut client = EchoClient::new(channel);
    let start = Instant::now();
    let mut stream = client
        .echo_stream(EchoStreamRequest { payload_size: payload_bytes, count })
        .await?
        .into_inner();

    let mut msgs: u64 = 0;
    let mut bytes: u64 = 0;
    while let Some(msg) = stream.next().await {
        let m = msg?;
        msgs += 1;
        bytes += m.payload.len() as u64;
    }
    let elapsed = start.elapsed().as_secs_f64();
    let stats = ThroughputStats {
        msgs,
        bytes,
        elapsed_s: elapsed,
        mbps: (bytes as f64 / 1e6) / elapsed,
        msgs_per_s: msgs as f64 / elapsed,
    };
    println!(
        "[throughput {}] msgs={} payload={}B  elapsed={:.3}s  {:.1} MB/s  {:.0} msg/s",
        label, msgs, payload_bytes, stats.elapsed_s, stats.mbps, stats.msgs_per_s,
    );
    Ok(stats)
}

fn print_latency_compare(tcp: LatencyStats, uds: LatencyStats) {
    let speedup = tcp.p50_us / uds.p50_us;
    println!("\n=== Latency: TCP vs UDS ===");
    println!("              p50 (us)   p95 (us)   p99 (us)   mean (us)");
    println!("  TCP        {:>9.1}  {:>9.1}  {:>9.1}  {:>9.1}", tcp.p50_us, tcp.p95_us, tcp.p99_us, tcp.mean_us);
    println!("  UDS        {:>9.1}  {:>9.1}  {:>9.1}  {:>9.1}", uds.p50_us, uds.p95_us, uds.p99_us, uds.mean_us);
    println!("  UDS p50 speedup vs TCP: {:.2}x", speedup);
}

fn print_throughput_compare(tcp: ThroughputStats, uds: ThroughputStats) {
    let speedup = uds.mbps / tcp.mbps;
    println!("\n=== Throughput: TCP vs UDS ===");
    println!("              MB/s        msg/s       elapsed (s)");
    println!("  TCP        {:>9.1}  {:>9.0}  {:>11.3}", tcp.mbps, tcp.msgs_per_s, tcp.elapsed_s);
    println!("  UDS        {:>9.1}  {:>9.0}  {:>11.3}", uds.mbps, uds.msgs_per_s, uds.elapsed_s);
    println!("  UDS speedup vs TCP: {:.2}x", speedup);
}

#[tokio::main(flavor = "multi_thread")]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();
    let socket_path = args.socket_path.clone().unwrap_or_else(|| default_socket_path(args.port));

    let want_tcp = matches!(args.transport, Transport::Tcp | Transport::Both);
    let want_uds = matches!(args.transport, Transport::Uds | Transport::Both);
    let want_latency = matches!(args.mode, Mode::Latency | Mode::Both);
    let want_throughput = matches!(args.mode, Mode::Throughput | Mode::Both);

    let mut tcp_lat = None;
    let mut uds_lat = None;
    let mut tcp_tp = None;
    let mut uds_tp = None;

    if want_tcp {
        let channel = dial_tcp(&args.host, args.port).await?;
        if want_latency {
            tcp_lat = Some(
                run_latency("tcp", channel.clone(), args.latency_iters, args.latency_payload_bytes)
                    .await?,
            );
        }
        if want_throughput {
            tcp_tp = Some(
                run_throughput(
                    "tcp",
                    channel,
                    args.throughput_payload_bytes,
                    args.throughput_count,
                )
                .await?,
            );
        }
    }

    if want_uds {
        let channel = dial_uds(socket_path.clone()).await?;
        if want_latency {
            uds_lat = Some(
                run_latency("uds", channel.clone(), args.latency_iters, args.latency_payload_bytes)
                    .await?,
            );
        }
        if want_throughput {
            uds_tp = Some(
                run_throughput(
                    "uds",
                    channel,
                    args.throughput_payload_bytes,
                    args.throughput_count,
                )
                .await?,
            );
        }
    }

    if let (Some(t), Some(u)) = (tcp_lat, uds_lat) {
        print_latency_compare(t, u);
    }
    if let (Some(t), Some(u)) = (tcp_tp, uds_tp) {
        print_throughput_compare(t, u);
    }

    Ok(())
}
