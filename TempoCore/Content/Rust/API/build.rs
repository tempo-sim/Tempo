// Copyright Tempo Simulation, LLC. All Rights Reserved

use std::path::PathBuf;
use walkdir::WalkDir;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let proto_dir = "proto";

    // Check if proto directory exists
    if !std::path::Path::new(proto_dir).exists() {
        println!("cargo:warning=Proto directory not found. Run gen_protos.py first.");
        return Ok(());
    }

    // Collect all proto files
    let protos: Vec<PathBuf> = WalkDir::new(proto_dir)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| {
            e.path()
                .extension()
                .map_or(false, |ext| ext == "proto")
        })
        .map(|e| e.path().to_path_buf())
        .collect();

    if protos.is_empty() {
        println!("cargo:warning=No proto files found. Run gen_protos.py first.");
        return Ok(());
    }

    // Use the vendored protoc so consumers don't need one on PATH.
    let protoc = protoc_bin_vendored::protoc_bin_path()?;
    std::env::set_var("PROTOC", protoc.as_os_str());

    // Configure tonic-build
    tonic_build::configure()
        .build_server(false) // Client only
        .build_client(true)
        .compile_protos(&protos, &[proto_dir])?;

    // Tell Cargo to rerun if any proto file changes
    println!("cargo:rerun-if-changed={}", proto_dir);

    Ok(())
}
