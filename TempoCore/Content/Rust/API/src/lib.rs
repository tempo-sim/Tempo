// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Tempo Simulation Rust Client API
//!
//! This crate provides a Rust client for interacting with Tempo simulation servers
//! via gRPC. It supports both synchronous and asynchronous operation using tokio.
//!
//! # Quick Start
//!
//! ```rust,no_run
//! use tempo::set_server;
//!
//! fn main() -> Result<(), tempo::TempoError> {
//!     // Connect to the Tempo server
//!     set_server("localhost", 10001)?;
//!
//!     // Use the API modules (e.g., tempo_core, tempo_sensors)
//!     Ok(())
//! }
//! ```

pub mod context;
pub mod error;
pub mod proto;
pub mod streaming;

// Re-export commonly used items
pub use context::{set_server, set_server_async, tempo_context, TempoContext};
pub use error::TempoError;
pub use streaming::SyncStreamIterator;

pub mod greeter;
pub mod tempo_agents_editor;
pub mod tempo_core;
pub mod tempo_core_editor;
pub mod tempo_geographic;
pub mod tempo_labels;
pub mod tempo_map_query;
pub mod tempo_movement;
pub mod tempo_sensors;
pub mod tempo_time;
pub mod tempo_world;
