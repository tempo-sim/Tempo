// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Streaming utilities for converting async streams to sync iterators.

use tokio::runtime::Handle;
use tokio_stream::StreamExt;

/// Helper to convert tonic async streams to synchronous iterators.
///
/// Drives the wrapped stream on the shared `crate::context::RUNTIME`. Each call
/// to `next()` blocks the calling thread until the next item is available.
pub struct SyncStreamIterator<T> {
    handle: Handle,
    stream: tonic::Streaming<T>,
}

impl<T> SyncStreamIterator<T> {
    /// Create a new `SyncStreamIterator` wrapping an async stream.
    pub fn new(stream: tonic::Streaming<T>) -> Self {
        Self {
            handle: crate::context::RUNTIME.handle().clone(),
            stream,
        }
    }
}

impl<T> Iterator for SyncStreamIterator<T> {
    type Item = Result<T, tonic::Status>;

    fn next(&mut self) -> Option<Self::Item> {
        self.handle.block_on(async { self.stream.next().await })
    }
}
