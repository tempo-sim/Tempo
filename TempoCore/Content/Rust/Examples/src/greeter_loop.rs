// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Example client that repeatedly calls greet_async every second.

use tempo::{greeter, set_server_async};
use tokio::time::{interval, Duration};

#[tokio::main]
async fn main() -> Result<(), tempo::TempoError> {
    // Connect to the Tempo server
    set_server_async("localhost", 10001).await;

    println!("Connected to Tempo server. Starting greeter loop...");

    let mut tick = interval(Duration::from_secs(1));
    let mut count = 0u32;

    loop {
        tick.tick().await;
        count += 1;

        let message = format!("Hello from Rust client #{}", count);
        match greeter::greet_async(message.clone()).await {
            Ok(response) => {
                println!("[{}] Sent: '{}' -> Received: '{}'", count, message, response.message);
            }
            Err(e) => {
                eprintln!("[{}] Error: {:?}", count, e);
            }
        }
    }
}
