// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Connection context management for Tempo gRPC client.
//!
//! This module provides a global singleton context that manages the gRPC channel
//! connection to the Tempo server, similar to Python's `_tempo_context.py`.

use once_cell::sync::Lazy;
use std::sync::Arc;
use tokio::sync::RwLock;
use tonic::transport::Channel;

use crate::error::TempoError;

/// Default server address.
pub const DEFAULT_ADDRESS: &str = "localhost";

/// Default server port.
pub const DEFAULT_PORT: u16 = 10001;

/// Maximum message size (1GB, matching Python client).
const MAX_MESSAGE_SIZE: usize = 1_000_000_000;

/// Global context for managing Tempo server connection.
pub struct TempoContext {
    address: String,
    port: u16,
    channel: Option<Channel>,
}

impl Default for TempoContext {
    fn default() -> Self {
        Self {
            address: DEFAULT_ADDRESS.to_string(),
            port: DEFAULT_PORT,
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
            channel: None,
        }
    }

    /// Set the server address and port. Resets any existing connection.
    pub fn set_server(&mut self, address: &str, port: u16) {
        self.address = address.to_string();
        self.port = port;
        self.channel = None;
    }

    /// Get a connected channel, creating one if necessary.
    pub async fn channel(&mut self) -> Result<Channel, TempoError> {
        if self.channel.is_none() {
            let endpoint = format!("http://{}:{}", self.address, self.port);
            let channel = Channel::from_shared(endpoint)?
                .initial_stream_window_size(MAX_MESSAGE_SIZE as u32)
                .initial_connection_window_size(MAX_MESSAGE_SIZE as u32)
                .connect()
                .await?;
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
}

/// Global singleton context.
pub static CONTEXT: Lazy<Arc<RwLock<TempoContext>>> =
    Lazy::new(|| Arc::new(RwLock::new(TempoContext::default())));

/// Get a reference to the global context.
pub fn tempo_context() -> Arc<RwLock<TempoContext>> {
    CONTEXT.clone()
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
pub fn set_server(address: &str, port: u16) -> Result<(), TempoError> {
    let rt = tokio::runtime::Runtime::new()?;
    rt.block_on(set_server_async(address, port));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_context() {
        let ctx = TempoContext::default();
        assert_eq!(ctx.address(), DEFAULT_ADDRESS);
        assert_eq!(ctx.port(), DEFAULT_PORT);
    }

    #[test]
    fn test_set_server() {
        let mut ctx = TempoContext::default();
        ctx.set_server("192.168.1.100", 12345);
        assert_eq!(ctx.address(), "192.168.1.100");
        assert_eq!(ctx.port(), 12345);
    }
}
