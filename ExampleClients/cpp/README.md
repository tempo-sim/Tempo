# Tempo C++ Example Clients

Three small programs that exercise the Tempo C++ wrapper API:

| Example | What it does |
|---|---|
| `greeter_loop` | Calls the Greeter `Greet` RPC every second and prints the response. |
| `sensor_playground` | Lists available sensors. If a lidar exists, streams 5 scans and logs basic stats. |
| `tempo_mcp` | Looks up an actor by name and prints its transform, velocity, and bounds. |

These mirror the Rust examples in `../Rust/`. They log to stdout — no GUI, no
interactive prompts.

## Prerequisites

1. **Generate and build the Tempo C++ API** by running a Tempo build with
   `TEMPO_GEN_CPP_API=1`. This produces:
   - Headers: `Plugins/Tempo/TempoCore/Content/Cpp/API/include/`
   - Static archive: `Plugins/Tempo/TempoCore/Content/Cpp/API/lib/<Platform>/libtempo.{a,lib}`
2. **CMake** 3.20 or later.
3. **OpenSSL 1.1** and **zlib**. The vendored gRPC was compiled against
   OpenSSL 1.x and references `SSL_get_peer_certificate`, which OpenSSL 3.x
   replaced with `SSL_get1_peer_certificate`. On macOS:
   `brew install openssl@1.1`.
4. A **C++20** compiler (clang 14+, gcc 11+, MSVC 19.30+). Required because
   the vendored absl headers depend on `std::weak_ordering`.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@1.1
cmake --build build --parallel
```

On Linux, point `OPENSSL_ROOT_DIR` at an OpenSSL 1.1 install (or rebuild gRPC
against your system OpenSSL).

## Run

Each example accepts optional positional args. Defaults connect to
`localhost:10001`.

```sh
./build/greeter_loop                          # localhost:10001
./build/greeter_loop my-host 10001
./build/sensor_playground                     # localhost:10001, 5 frames
./build/sensor_playground localhost 10001 20  # stream 20 frames
./build/tempo_mcp                             # actor "Vehicle"
./build/tempo_mcp localhost 10001 PlayerActor
```
