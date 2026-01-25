// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Streaming utilities for converting async streams to sync iterators.

use tokio_stream::StreamExt;

/// Helper to convert tonic async streams to synchronous iterators.
///
/// This wraps an async `tonic::Streaming` and provides a blocking `Iterator` interface.
/// Each call to `next()` blocks until the next item is available from the stream.
pub struct SyncStreamIterator<T> {
    rt: tokio::runtime::Runtime,
    stream: tonic::Streaming<T>,
}

impl<T> SyncStreamIterator<T> {
    /// Create a new `SyncStreamIterator` wrapping an async stream.
    ///
    /// Takes ownership of both the tokio runtime and the stream.
    pub fn new(rt: tokio::runtime::Runtime, stream: tonic::Streaming<T>) -> Self {
        Self { rt, stream }
    }
}

impl<T> Iterator for SyncStreamIterator<T> {
    type Item = Result<T, tonic::Status>;

    fn next(&mut self) -> Option<Self::Item> {
        self.rt.block_on(async { self.stream.next().await })
    }
}
