// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Echo gRPC server that listens on both TCP and a Unix-domain socket simultaneously.
//!
//! Used by `tempo-bench-client` to measure transport overhead (TCP vs UDS) without a
//! running Tempo sim. The same Echo service is bound on both listeners so the only
//! axis the client varies is the transport.

use std::path::PathBuf;
use std::pin::Pin;

use clap::Parser;
use tokio::net::{TcpListener, UnixListener};
use tokio_stream::wrappers::{TcpListenerStream, UnixListenerStream};
use tokio_stream::Stream;
use tonic::{transport::Server, Request, Response, Status};

use tempo_transport_benchmark::default_socket_path;
use tempo_transport_benchmark::proto::echo_server::{Echo, EchoServer};
use tempo_transport_benchmark::proto::{EchoRequest, EchoResponse, EchoStreamRequest};

#[derive(Parser, Debug)]
#[command(about = "Tempo transport benchmark: echo server (TCP + UDS).")]
struct Args {
    /// TCP port to bind.
    #[arg(long, default_value_t = 50051)]
    port: u16,

    /// Override the UDS path. Defaults to `/tmp/tempo-bench-<port>.sock` (POSIX) or the
    /// platform-equivalent on Windows.
    #[arg(long)]
    socket_path: Option<PathBuf>,
}

#[derive(Default)]
struct EchoSvc;

#[tonic::async_trait]
impl Echo for EchoSvc {
    type EchoStreamStream =
        Pin<Box<dyn Stream<Item = Result<EchoResponse, Status>> + Send + 'static>>;

    async fn echo(&self, req: Request<EchoRequest>) -> Result<Response<EchoResponse>, Status> {
        let payload = req.into_inner().payload;
        Ok(Response::new(EchoResponse { payload, seq: 0 }))
    }

    async fn echo_stream(
        &self,
        req: Request<EchoStreamRequest>,
    ) -> Result<Response<Self::EchoStreamStream>, Status> {
        let req = req.into_inner();
        let payload_size = req.payload_size as usize;
        let count = req.count as u64;
        let payload: Vec<u8> = vec![0u8; payload_size];

        let stream = async_stream::try_stream! {
            for seq in 0..count {
                yield EchoResponse { payload: payload.clone(), seq };
            }
        };
        Ok(Response::new(Box::pin(stream)))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let args = Args::parse();
    let socket_path = args.socket_path.unwrap_or_else(|| default_socket_path(args.port));

    let tcp_addr: std::net::SocketAddr = ([0, 0, 0, 0], args.port).into();
    let tcp_listener = TcpListener::bind(tcp_addr).await?;
    println!("[tempo-bench-server] TCP listening on {}", tcp_addr);

    // Clean up any stale UDS file from a prior unclean shutdown.
    let _ = std::fs::remove_file(&socket_path);
    if let Some(parent) = socket_path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let uds_listener = UnixListener::bind(&socket_path)?;
    println!("[tempo-bench-server] UDS listening on {}", socket_path.display());

    let svc_tcp = EchoServer::new(EchoSvc::default());
    let svc_uds = EchoServer::new(EchoSvc::default());

    let tcp_server = Server::builder()
        .add_service(svc_tcp)
        .serve_with_incoming(TcpListenerStream::new(tcp_listener));
    let uds_server = Server::builder()
        .add_service(svc_uds)
        .serve_with_incoming(UnixListenerStream::new(uds_listener));

    let socket_path_for_cleanup = socket_path.clone();
    let shutdown = async move {
        let _ = tokio::signal::ctrl_c().await;
        let _ = std::fs::remove_file(&socket_path_for_cleanup);
    };

    tokio::select! {
        res = tcp_server => res?,
        res = uds_server => res?,
        _ = shutdown => {
            println!("[tempo-bench-server] shutting down");
        }
    }
    Ok(())
}
