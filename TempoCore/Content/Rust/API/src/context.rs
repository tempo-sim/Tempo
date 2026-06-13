// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Connection context management for Tempo gRPC client.
//!
//! This module provides a global singleton context that manages the gRPC channel
//! connection to the Tempo server, similar to Python's `_tempo_context.py`.
//!
//! The default endpoint is a Unix domain socket (matching the Tempo server's default
//! UDS listener), so a freshly-imported client connects to a same-host sim with no
//! configuration. Call [`set_server_async`]/[`set_server`] to switch to TCP for a
//! remote sim, or [`set_unix_socket_async`]/[`set_unix_socket`] to override the
//! socket path.
//!
//! On non-Unix targets, tokio does not expose `UnixStream`, so [`Endpoint::Uds`]
//! cannot be connected — attempting to do so returns a connection error. Users on
//! those platforms should call [`set_server`] explicitly.

use once_cell::sync::Lazy;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::runtime::Runtime;
use tokio::sync::RwLock;
use tonic::transport::Channel;

use crate::error::TempoError;

/// Default server address.
pub const DEFAULT_ADDRESS: &str = "localhost";

/// Default server port.
pub const DEFAULT_PORT: u16 = 10001;

/// Maximum message size (1GB, matching Python client).
pub const MAX_MESSAGE_SIZE: usize = 1_000_000_000;

/// Where the client should connect. UDS is the default since the Tempo server always
/// listens on a same-host Unix-domain socket alongside TCP.
#[derive(Debug, Clone)]
pub enum Endpoint {
    Tcp { address: String, port: u16 },
    Uds { path: PathBuf },
}

/// Return the default Unix-domain-socket path the Tempo server listens on for the given port.
///
/// Mirrors `UTempoCoreUtils::GetDefaultUnixSocketPath` so the client picks up the same
/// socket the server creates without any explicit configuration.
pub fn default_unix_socket_path(port: u16) -> PathBuf {
    #[cfg(windows)]
    {
        let base = std::env::var_os("LOCALAPPDATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(r"C:\Windows\Temp"));
        base.join("Temp").join(format!("tempo-{}.sock", port))
    }
    #[cfg(not(windows))]
    {
        PathBuf::from(format!("/tmp/tempo-{}.sock", port))
    }
}

/// Global context for managing Tempo server connection.
pub struct TempoContext {
    endpoint: Endpoint,
    channel: Option<Channel>,
}

impl Default for TempoContext {
    fn default() -> Self {
        // On Unix the default is UDS, matching the server's same-host default. On platforms
        // where tokio does not support `UnixStream` (Windows) we fall back to TCP so the
        // out-of-the-box client still connects; users on those platforms can still call
        // `set_unix_socket` explicitly if they have a working AF_UNIX implementation wired up.
        #[cfg(unix)]
        let endpoint = Endpoint::Uds {
            path: default_unix_socket_path(DEFAULT_PORT),
        };
        #[cfg(not(unix))]
        let endpoint = Endpoint::Tcp {
            address: DEFAULT_ADDRESS.to_string(),
            port: DEFAULT_PORT,
        };
        Self {
            endpoint,
            channel: None,
        }
    }
}

impl TempoContext {
    /// Create a new context that will connect via TCP to the given address and port.
    pub fn new(address: &str, port: u16) -> Self {
        Self {
            endpoint: Endpoint::Tcp {
                address: address.to_string(),
                port,
            },
            channel: None,
        }
    }

    /// Switch to TCP at the given address and port. Resets any existing connection.
    pub fn set_server(&mut self, address: &str, port: u16) {
        self.endpoint = Endpoint::Tcp {
            address: address.to_string(),
            port,
        };
        self.channel = None;
    }

    /// Switch to a Unix-domain socket. `path` overrides the default per-port path.
    pub fn set_unix_socket(&mut self, path: PathBuf) {
        self.endpoint = Endpoint::Uds { path };
        self.channel = None;
    }

    /// Get a connected channel, creating one if necessary.
    pub async fn channel(&mut self) -> Result<Channel, TempoError> {
        if self.channel.is_none() {
            let channel = connect(&self.endpoint).await?;
            self.channel = Some(channel);
        }
        Ok(self.channel.clone().unwrap())
    }

    /// Get the active endpoint description.
    pub fn endpoint(&self) -> &Endpoint {
        &self.endpoint
    }
}

async fn connect(endpoint: &Endpoint) -> Result<Channel, TempoError> {
    match endpoint {
        Endpoint::Tcp { address, port } => {
            let uri = format!("http://{}:{}", address, port);
            let channel = Channel::from_shared(uri)?
                .initial_stream_window_size(MAX_MESSAGE_SIZE as u32)
                .initial_connection_window_size(MAX_MESSAGE_SIZE as u32)
                .connect()
                .await?;
            Ok(channel)
        }
        #[cfg(unix)]
        Endpoint::Uds { path } => {
            use hyper_util::rt::TokioIo;
            use tokio::net::UnixStream;
            use tonic::transport::{Endpoint as TonicEndpoint, Uri};
            use tower::service_fn;
            let path = path.clone();
            // The URI here is unused — the connector ignores it — but tonic requires a
            // syntactically valid one to seed the Endpoint builder.
            let channel = TonicEndpoint::from_static("http://[::]:0")
                .initial_stream_window_size(MAX_MESSAGE_SIZE as u32)
                .initial_connection_window_size(MAX_MESSAGE_SIZE as u32)
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
        Endpoint::Uds { .. } => Err(TempoError::connection(
            "Unix domain sockets are not supported by tokio on this platform; call set_server() to use TCP.",
        )),
    }
}

/// Global singleton context.
pub static CONTEXT: Lazy<Arc<RwLock<TempoContext>>> =
    Lazy::new(|| Arc::new(RwLock::new(TempoContext::default())));

/// Shared multi-thread tokio runtime used by the sync API surface.
pub static RUNTIME: Lazy<Runtime> = Lazy::new(|| {
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .expect("Failed to build Tempo tokio runtime")
});

/// Get a reference to the global context.
pub fn tempo_context() -> Arc<RwLock<TempoContext>> {
    CONTEXT.clone()
}

/// Get a connected channel to the Tempo server.
///
/// The returned [`Channel`] is a cheap, independent handle that callers hold
/// *without* the lock, so the lock is never held across an RPC await. The
/// steady-state path (channel already connected) takes only a shared read
/// lock, so concurrent RPCs don't serialize on `CONTEXT`. Only the first
/// connect — or the first call after [`TempoContext::set_server`] resets the
/// channel — takes the write lock to lazily establish the connection.
pub async fn connected_channel() -> Result<Channel, TempoError> {
    // Fast path: shared read lock, clone the existing channel.
    {
        let ctx = CONTEXT.read().await;
        if let Some(channel) = ctx.channel.clone() {
            return Ok(channel);
        }
    }
    // Slow path: exclusive lock to connect once. `channel()` re-checks under
    // the write lock, so a racing writer that already connected just clones.
    let mut ctx = CONTEXT.write().await;
    ctx.channel().await
}

/// Switch to a TCP server endpoint at the given address and port (async version).
pub async fn set_server_async(address: &str, port: u16) {
    let mut ctx = CONTEXT.write().await;
    ctx.set_server(address, port);
}

/// Switch to a TCP server endpoint at the given address and port (sync version).
///
/// # Panics
///
/// Panics if called from within an async context. Use `set_server_async` instead.
pub fn set_server(address: &str, port: u16) {
    RUNTIME.block_on(set_server_async(address, port));
}

/// Switch to a Unix-domain socket at `path` (async version).
pub async fn set_unix_socket_async(path: impl Into<PathBuf>) {
    let mut ctx = CONTEXT.write().await;
    ctx.set_unix_socket(path.into());
}

/// Switch to a Unix-domain socket at `path` (sync version).
///
/// # Panics
///
/// Panics if called from within an async context. Use `set_unix_socket_async` instead.
pub fn set_unix_socket(path: impl Into<PathBuf>) {
    RUNTIME.block_on(set_unix_socket_async(path));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[cfg(unix)]
    #[test]
    fn test_default_context_is_uds_on_unix() {
        let ctx = TempoContext::default();
        match ctx.endpoint() {
            Endpoint::Uds { path } => {
                assert_eq!(path, &default_unix_socket_path(DEFAULT_PORT));
            }
            other => panic!("expected UDS default, got {:?}", other),
        }
    }

    #[cfg(not(unix))]
    #[test]
    fn test_default_context_is_tcp_off_unix() {
        let ctx = TempoContext::default();
        match ctx.endpoint() {
            Endpoint::Tcp { address, port } => {
                assert_eq!(address, DEFAULT_ADDRESS);
                assert_eq!(*port, DEFAULT_PORT);
            }
            other => panic!("expected TCP default, got {:?}", other),
        }
    }

    #[test]
    fn test_set_server_switches_to_tcp() {
        let mut ctx = TempoContext::default();
        ctx.set_server("192.168.1.100", 12345);
        match ctx.endpoint() {
            Endpoint::Tcp { address, port } => {
                assert_eq!(address, "192.168.1.100");
                assert_eq!(*port, 12345);
            }
            other => panic!("expected TCP after set_server, got {:?}", other),
        }
    }

    #[test]
    fn test_set_unix_socket_overrides_path() {
        let mut ctx = TempoContext::default();
        ctx.set_unix_socket(PathBuf::from("/tmp/custom.sock"));
        match ctx.endpoint() {
            Endpoint::Uds { path } => {
                assert_eq!(path, &PathBuf::from("/tmp/custom.sock"));
            }
            other => panic!("expected UDS, got {:?}", other),
        }
    }

    #[test]
    fn test_default_socket_path_is_port_keyed() {
        let p_a = default_unix_socket_path(10001);
        let p_b = default_unix_socket_path(10002);
        assert_ne!(p_a, p_b);
        assert!(p_a.to_string_lossy().contains("10001"));
        assert!(p_b.to_string_lossy().contains("10002"));
    }

    #[cfg(unix)]
    #[test]
    fn test_default_socket_path_uses_tmp_on_unix() {
        assert_eq!(
            default_unix_socket_path(10001),
            PathBuf::from("/tmp/tempo-10001.sock")
        );
    }

    #[test]
    fn test_round_trip_tcp_then_uds() {
        let mut ctx = TempoContext::default();
        ctx.set_server("127.0.0.1", 10001);
        assert!(matches!(ctx.endpoint(), Endpoint::Tcp { .. }));
        ctx.set_unix_socket(PathBuf::from("/tmp/back.sock"));
        match ctx.endpoint() {
            Endpoint::Uds { path } => assert_eq!(path, &PathBuf::from("/tmp/back.sock")),
            other => panic!("expected UDS after round trip, got {:?}", other),
        }
    }
}
