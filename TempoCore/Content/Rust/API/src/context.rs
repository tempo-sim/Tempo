// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Connection context management for Tempo gRPC client.
//!
//! This module provides a global singleton context that manages the gRPC channel
//! connection to the Tempo server, similar to Python's `_tempo_context.py`.

use once_cell::sync::Lazy;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tokio::runtime::Runtime;
use tokio::sync::RwLock;
use tonic::transport::Channel;

use crate::error::TempoError;

/// Default server address.
pub const DEFAULT_ADDRESS: &str = "localhost";

/// Default server port.
pub const DEFAULT_PORT: u16 = 10001;

/// The socket name a server uses when the UnixSocket transport is selected without a path.
pub const DEFAULT_SOCKET_NAME: &str = "tempo.sock";

/// Maximum message size (1GB, matching Python client).
pub const MAX_MESSAGE_SIZE: usize = 1_000_000_000;

/// The directory a bare socket name lands in. Mirrors the server's rule (see
/// `TempoServerEndpoint::GetDefaultSocketDirectory`) so that naming a socket `"sim-a.sock"`
/// means the same file on both sides.
#[cfg(unix)]
pub fn default_socket_directory() -> PathBuf {
    #[cfg(target_os = "linux")]
    {
        // Set for a logged-in user on any systemd distro: user-private, and cleared when the
        // session ends, so sockets do not outlive the login.
        if let Ok(runtime_dir) = std::env::var("XDG_RUNTIME_DIR") {
            if !runtime_dir.is_empty() {
                return PathBuf::from(runtime_dir).join("tempo");
            }
        }
    }
    // SAFETY: getuid is always successful and touches no memory we own.
    PathBuf::from(format!("/tmp/tempo-{}", unsafe { libc::getuid() }))
}

/// A bare name (`"sim-a.sock"`) goes in [`default_socket_directory`]; anything else is a path.
/// An empty path means [`DEFAULT_SOCKET_NAME`] in that directory, mirroring a server configured
/// for the UnixSocket transport without a path. The result is absolute, because gRPC resolves a
/// relative socket path against the client's working directory, which is rarely the server's.
fn resolve_socket_path(path: &Path) -> PathBuf {
    let path = if path.as_os_str().is_empty() {
        Path::new(DEFAULT_SOCKET_NAME)
    } else {
        path
    };

    #[cfg(unix)]
    let path = if path.parent().is_some_and(|parent| parent.as_os_str().is_empty()) {
        default_socket_directory().join(path)
    } else {
        path.to_path_buf()
    };
    #[cfg(not(unix))]
    let path = path.to_path_buf();

    if path.is_absolute() {
        path
    } else {
        std::env::current_dir().map_or(path.clone(), |dir| dir.join(path))
    }
}

/// Global context for managing Tempo server connection.
pub struct TempoContext {
    address: String,
    port: u16,
    // Some when connecting over a Unix domain socket, which takes precedence over address/port.
    // Only one transport is used at a time, mirroring the server.
    socket_path: Option<PathBuf>,
    channel: Option<Channel>,
}

impl Default for TempoContext {
    /// Starts from the environment, so one variable can point a client and a sim at the same
    /// endpoint without either of them changing code:
    ///
    /// - `TEMPO_SERVER_SOCKET` — a Unix domain socket, as `-ServerSocket=` takes
    /// - `TEMPO_SERVER_ADDRESS` / `TEMPO_SERVER_PORT` — as `-ServerPort=` takes
    ///
    /// `TEMPO_SERVER_SOCKET` wins when both are set, matching the server's own precedence.
    fn default() -> Self {
        fn non_empty(name: &str) -> Option<String> {
            std::env::var(name)
                .ok()
                .map(|value| value.trim().to_string())
                .filter(|value| !value.is_empty())
        }

        Self {
            address: non_empty("TEMPO_SERVER_ADDRESS").unwrap_or_else(|| DEFAULT_ADDRESS.to_string()),
            port: non_empty("TEMPO_SERVER_PORT")
                .and_then(|port| port.parse().ok())
                .unwrap_or(DEFAULT_PORT),
            socket_path: non_empty("TEMPO_SERVER_SOCKET").map(|path| resolve_socket_path(Path::new(&path))),
            channel: None,
        }
    }
}

impl TempoContext {
    /// Create a new context with the given server address and port.
    pub fn new(address: &str, port: u16) -> Self {
        Self {
            address: address.to_string(),
            port,
            socket_path: None,
            channel: None,
        }
    }

    /// Set the server address and port. Resets any existing connection.
    pub fn set_server(&mut self, address: &str, port: u16) {
        self.address = address.to_string();
        self.port = port;
        self.socket_path = None;
        self.channel = None;
    }

    /// Connect over a Unix domain socket instead of TCP, for a server on this machine using the
    /// UnixSocket transport. See [`resolve_socket_path`] for how the path is interpreted.
    /// Resets any existing connection.
    pub fn set_socket<P: AsRef<Path>>(&mut self, path: P) {
        self.socket_path = Some(resolve_socket_path(path.as_ref()));
        self.channel = None;
    }

    /// Get a connected channel, creating one if necessary.
    pub async fn channel(&mut self) -> Result<Channel, TempoError> {
        if self.channel.is_none() {
            let channel = match &self.socket_path {
                Some(socket_path) => connect_socket(socket_path.clone()).await?,
                None => {
                    let endpoint = format!("http://{}:{}", self.address, self.port);
                    Channel::from_shared(endpoint)?
                        .initial_stream_window_size(MAX_MESSAGE_SIZE as u32)
                        .initial_connection_window_size(MAX_MESSAGE_SIZE as u32)
                        .connect()
                        .await?
                }
            };
            self.channel = Some(channel);
        }
        Ok(self.channel.clone().unwrap())
    }

    /// Get the current server address.
    pub fn address(&self) -> &str {
        &self.address
    }

    /// Get the current server port.
    pub fn port(&self) -> u16 {
        self.port
    }

    /// The resolved socket path, or `None` when connecting over TCP.
    pub fn socket_path(&self) -> Option<&Path> {
        self.socket_path.as_deref()
    }

    /// The gRPC target this context connects to, for logs and error messages.
    pub fn target(&self) -> String {
        match &self.socket_path {
            // gRPC's naming scheme for a filesystem socket. The path is absolute, so the single
            // slash after the scheme is the leading slash of the path itself.
            Some(socket_path) => format!("unix:{}", socket_path.display()),
            None => format!("{}:{}", self.address, self.port),
        }
    }
}

/// Opens a channel over a Unix domain socket.
///
/// tonic has no `unix:` target of its own: the endpoint URI only supplies the HTTP/2 authority,
/// and the connector decides what is actually dialed, so the host in it is never resolved.
#[cfg(unix)]
async fn connect_socket(socket_path: PathBuf) -> Result<Channel, TempoError> {
    let channel = Channel::from_static("http://localhost")
        .initial_stream_window_size(MAX_MESSAGE_SIZE as u32)
        .initial_connection_window_size(MAX_MESSAGE_SIZE as u32)
        .connect_with_connector(tower::service_fn(move |_: http::Uri| {
            let socket_path = socket_path.clone();
            async move {
                let stream = tokio::net::UnixStream::connect(socket_path).await?;
                Ok::<_, std::io::Error>(hyper_util::rt::TokioIo::new(stream))
            }
        }))
        .await?;
    Ok(channel)
}

#[cfg(not(unix))]
async fn connect_socket(socket_path: PathBuf) -> Result<Channel, TempoError> {
    Err(TempoError::UnsupportedTransport(format!(
        "Unix domain sockets are not supported on this platform (requested {}). \
         Use set_server(address, port).",
        socket_path.display()
    )))
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

/// Set the server address and port (async version).
pub async fn set_server_async(address: &str, port: u16) {
    let mut ctx = CONTEXT.write().await;
    ctx.set_server(address, port);
}

/// Set the server address and port (sync version).
///
/// # Panics
///
/// Panics if called from within an async context. Use `set_server_async` instead.
pub fn set_server(address: &str, port: u16) {
    RUNTIME.block_on(set_server_async(address, port));
}

/// Connect over a Unix domain socket (async version).
/// See [`TempoContext::set_socket`] for how the path is interpreted.
pub async fn set_socket_async<P: AsRef<Path>>(path: P) {
    let mut ctx = CONTEXT.write().await;
    ctx.set_socket(path);
}

/// Connect over a Unix domain socket (sync version).
/// See [`TempoContext::set_socket`] for how the path is interpreted.
///
/// # Panics
///
/// Panics if called from within an async context. Use `set_socket_async` instead.
pub fn set_socket<P: AsRef<Path>>(path: P) {
    RUNTIME.block_on(set_socket_async(path));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_context() {
        // Constructed explicitly rather than via Default, which reads the environment.
        let ctx = TempoContext::new(DEFAULT_ADDRESS, DEFAULT_PORT);
        assert_eq!(ctx.address(), DEFAULT_ADDRESS);
        assert_eq!(ctx.port(), DEFAULT_PORT);
        assert_eq!(ctx.socket_path(), None);
        assert_eq!(ctx.target(), format!("{DEFAULT_ADDRESS}:{DEFAULT_PORT}"));
    }

    #[test]
    fn test_set_server() {
        let mut ctx = TempoContext::new(DEFAULT_ADDRESS, DEFAULT_PORT);
        ctx.set_server("192.168.1.100", 12345);
        assert_eq!(ctx.address(), "192.168.1.100");
        assert_eq!(ctx.port(), 12345);
        assert_eq!(ctx.target(), "192.168.1.100:12345");
    }

    #[cfg(unix)]
    #[test]
    fn test_set_socket() {
        let mut ctx = TempoContext::new(DEFAULT_ADDRESS, DEFAULT_PORT);

        // An absolute path is used as given, and the target carries gRPC's "unix:" scheme.
        ctx.set_socket("/tmp/tempo-test.sock");
        assert_eq!(ctx.socket_path(), Some(Path::new("/tmp/tempo-test.sock")));
        assert_eq!(ctx.target(), "unix:/tmp/tempo-test.sock");

        // A bare name lands in the shared default directory.
        ctx.set_socket("sim-a.sock");
        assert_eq!(
            ctx.socket_path(),
            Some(default_socket_directory().join("sim-a.sock").as_path())
        );

        // An empty path means the default name there, as it does for the server.
        ctx.set_socket("");
        assert_eq!(
            ctx.socket_path(),
            Some(default_socket_directory().join(DEFAULT_SOCKET_NAME).as_path())
        );

        // Every resolved path is absolute, whatever was asked for.
        ctx.set_socket("./relative/tempo.sock");
        assert!(ctx.socket_path().is_some_and(Path::is_absolute));

        // Going back to TCP drops the socket.
        ctx.set_server(DEFAULT_ADDRESS, DEFAULT_PORT);
        assert_eq!(ctx.socket_path(), None);
    }
}
