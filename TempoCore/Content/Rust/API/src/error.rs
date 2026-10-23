// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Error types for the Tempo client API.

use thiserror::Error;

/// Error type for Tempo client operations.
#[derive(Error, Debug)]
pub enum TempoError {
    /// Connection error when establishing gRPC channel.
    #[error("Connection error: {0}")]
    Connection(#[from] tonic::transport::Error),

    /// RPC error returned by the server.
    #[error("RPC error: {0}")]
    Rpc(#[from] tonic::Status),

    /// Invalid URI error when parsing server address.
    #[error("Invalid URI: {0}")]
    InvalidUri(#[from] http::uri::InvalidUri),

    /// The requested transport is not available on this platform.
    #[error("Unsupported transport: {0}")]
    UnsupportedTransport(String),

    /// The linked generated classes were made from a different build than the sim is running.
    #[error("{0}")]
    SchemaMismatch(String),

    /// The server's answer was not the one this call's type says it must be. Raised by the typed
    /// World API, where the expected `Value` variant is fixed when the accessor is generated, so a
    /// mismatch means the stubs and the sim disagree about a property.
    #[error("{0}")]
    Protocol(String),
}
