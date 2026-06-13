// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Shared helpers for the Tempo transport benchmark.

pub mod proto {
    tonic::include_proto!("tempo_bench");
}

use std::path::PathBuf;

/// Default UDS path used by the benchmark when none is supplied. Mirrors the convention used
/// by the Tempo server (`/tmp/tempo-bench-<port>.sock`) but with a distinct prefix so the
/// benchmark never collides with a real sim's socket.
pub fn default_socket_path(port: u16) -> PathBuf {
    #[cfg(windows)]
    {
        let base = std::env::var_os("LOCALAPPDATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(r"C:\Windows\Temp"));
        base.join("Temp").join(format!("tempo-bench-{}.sock", port))
    }
    #[cfg(not(windows))]
    {
        PathBuf::from(format!("/tmp/tempo-bench-{}.sock", port))
    }
}
