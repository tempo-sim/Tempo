# Tempo C++ Example Clients

Small programs that exercise the Tempo C++ wrapper API:

| Example | What it does |
|---|---|
| `sensor_playground` | Lists available sensors. If a lidar exists, streams 5 scans and logs basic stats. |
| `world_playground` | Looks up an actor by name and prints its transform, velocity, and bounds. |
| `movement_playground` | Lists commandable and navigable pawns, then sends one of each movement command (vehicle, velocity, acceleration, move-to-location) against the first pawn. |

These mirror the Rust examples in `../Rust/`. They log to stdout — no GUI, no
interactive prompts.

## Prerequisites

1. **The Tempo C++ client**, from a Tempo build with `TEMPO_GEN_CPP_API=1` (into
   `Plugins/Tempo/TempoCore/Content/Cpp/API/`), or a packaged one (`Packaged/API/Cpp/`, which
   `Scripts/Package.sh` writes with the same variable set).
2. **CMake** 3.20 or later.
3. A **C++20** compiler (clang 14+, gcc 11+, MSVC 19.30+). Required because the vendored absl
   headers depend on `std::weak_ordering`.

Nothing else: the client links the shared `tempogrpc` library, which carries gRPC, protobuf,
abseil, OpenSSL and zlib inside it. On Windows, copy `tempogrpc.dll` next to the executables.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Against a packaged client instead of the one in this checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTempo_DIR=<project>/Packaged/API/Cpp
```

## Run

Each example accepts optional positional args. Defaults connect to
`localhost:10001`.

```sh
./build/sensor_playground                     # localhost:10001, 5 frames
./build/sensor_playground localhost 10001 20  # stream 20 frames
./build/world_playground                      # actor "Vehicle"
./build/world_playground localhost 10001 PlayerActor
./build/movement_playground                   # localhost:10001
./build/movement_playground localhost 10001
```
