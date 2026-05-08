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
}
