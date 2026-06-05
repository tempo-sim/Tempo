// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Compile Tempo .proto files to Rust source via tonic-build, writing one
// <package>.rs per proto package into --out-dir. Invoked by gen_rust_api.py.

use std::collections::BTreeMap;
use std::path::PathBuf;
use std::process::ExitCode;

fn usage(arg0: &str) -> String {
    format!(
        "Usage: {arg0} \\\n  \
         --proto-dir <dir>           (required, repeatable) Directory of .proto files to compile.\n  \
         --include-dir <dir>         (repeatable) Additional protoc -I path; not compiled, only resolved.\n  \
         --out-dir <dir>             (required) Where generated <package>.rs files are written.\n  \
         --extern-path <P>=<R>       (repeatable) Map proto path P (e.g. \".TempoCore\") to Rust path R \n                              (e.g. \"::tempo_sim::proto::tempo_core\"); tonic-build will reference \n                              these types instead of emitting them."
    )
}

struct Args {
    proto_dirs: Vec<PathBuf>,
    include_dirs: Vec<PathBuf>,
    out_dir: Option<PathBuf>,
    extern_paths: BTreeMap<String, String>,
}

fn parse_args() -> Result<Args, String> {
    let mut args = std::env::args();
    let _arg0 = args.next();
    let mut out = Args {
        proto_dirs: Vec::new(),
        include_dirs: Vec::new(),
        out_dir: None,
        extern_paths: BTreeMap::new(),
    };
    while let Some(flag) = args.next() {
        let mut val = || args.next().ok_or_else(|| format!("missing value for {flag}"));
        match flag.as_str() {
            "--proto-dir" => out.proto_dirs.push(PathBuf::from(val()?)),
            "--include-dir" => out.include_dirs.push(PathBuf::from(val()?)),
            "--out-dir" => out.out_dir = Some(PathBuf::from(val()?)),
            "--extern-path" => {
                let v = val()?;
                let (p, r) = v.split_once('=').ok_or_else(|| {
                    format!("--extern-path expects <proto-path>=<rust-path>, got: {v}")
                })?;
                out.extern_paths.insert(p.to_string(), r.to_string());
            }
            "-h" | "--help" => return Err("help".to_string()),
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    if out.proto_dirs.is_empty() {
        return Err("at least one --proto-dir is required".to_string());
    }
    if out.out_dir.is_none() {
        return Err("--out-dir is required".to_string());
    }
    Ok(out)
}

fn collect_protos(roots: &[PathBuf]) -> Vec<PathBuf> {
    let mut out = Vec::new();
    for root in roots {
        for entry in walkdir::WalkDir::new(root).into_iter().filter_map(|e| e.ok()) {
            if entry.file_type().is_file()
                && entry.path().extension().map_or(false, |ext| ext == "proto")
            {
                out.push(entry.path().to_path_buf());
            }
        }
    }
    out
}

fn main() -> ExitCode {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            let arg0 = std::env::args().next().unwrap_or_else(|| "tempo-sim-codegen".into());
            if e == "help" {
                println!("{}", usage(&arg0));
                return ExitCode::SUCCESS;
            }
            eprintln!("error: {e}\n\n{}", usage(&arg0));
            return ExitCode::FAILURE;
        }
    };

    let out_dir = args.out_dir.unwrap();
    if let Err(e) = std::fs::create_dir_all(&out_dir) {
        eprintln!("error: failed to create out-dir {}: {e}", out_dir.display());
        return ExitCode::FAILURE;
    }

    let protos = collect_protos(&args.proto_dirs);
    if protos.is_empty() {
        eprintln!("error: no .proto files found under {:?}", args.proto_dirs);
        return ExitCode::FAILURE;
    }

    // Honor PROTOC if set; otherwise fall back to the vendored binary.
    if std::env::var_os("PROTOC").is_none() {
        match protoc_bin_vendored::protoc_bin_path() {
            Ok(p) => std::env::set_var("PROTOC", p.as_os_str()),
            Err(e) => {
                eprintln!("error: vendored protoc not available: {e}");
                return ExitCode::FAILURE;
            }
        }
    }

    let mut includes: Vec<PathBuf> = args.proto_dirs.clone();
    includes.extend(args.include_dirs.iter().cloned());

    let mut builder = tonic_build::configure()
        .build_server(false)
        .build_client(true)
        .out_dir(&out_dir);
    for (proto_path, rust_path) in &args.extern_paths {
        builder = builder.extern_path(proto_path.clone(), rust_path.clone());
    }

    if let Err(e) = builder.compile_protos(&protos, &includes) {
        eprintln!("error: tonic-build failed: {e}");
        return ExitCode::FAILURE;
    }

    ExitCode::SUCCESS
}
