// Copyright Tempo Simulation, LLC. All Rights Reserved

//! TestRustAPI.sh's teardown: ask the sim to exit, as the Python harness does, before it is
//! signalled. On Windows a signal is TerminateProcess, and a sim terminated mid-load once stayed
//! stuck in its termination, holding the build tree until a reboot. The typed crate builds this
//! same file as its own example.

use std::time::Duration;

fn main() {
    let socket = std::env::var("TEMPO_SERVER_SOCKET").unwrap_or_default();
    if socket.trim().is_empty() {
        let port: u16 = std::env::var("TEMPO_SERVER_PORT")
            .ok()
            .and_then(|s| s.parse().ok())
            .unwrap_or(10001);
        tempo_sim::set_server("localhost", port);
    } else {
        tempo_sim::set_socket(socket.trim());
    }
    tempo_sim::set_request_timeout(Some(Duration::from_secs(10)));
    if let Err(error) = tempo_sim::tempo_core::quit() {
        eprintln!("the sim did not take the quit request: {error}");
        std::process::exit(1);
    }
}
