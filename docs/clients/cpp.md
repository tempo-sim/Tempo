# C++ Client

A drop-in C++ client library, generated at build time when `TEMPO_GEN_CPP_API=1`.

```bash
export TEMPO_GEN_CPP_API=1
Plugins/Tempo/Scripts/Build.sh
```

## What you get

```text
TempoCore/Content/Cpp/API/
├── include/
│   ├── tempo.h                      # umbrella header — include this and you're done
│   ├── tempo/                       # the high-level client surface
│   │   ├── context.h                #   set_server, TempoContext
│   │   ├── error.h                  #   TempoError
│   │   ├── result.h                 #   Result<T>
│   │   ├── streaming.h              #   ServerStream<T>
│   │   └── <service>.h              #   one per gRPC service (tempo_core, tempo_world, …)
│   └── proto/
│       └── <Package>/<File>.pb.h    # protoc-generated message types
│       └── <Package>/<File>.grpc.pb.h
├── lib/
│   └── <Platform>/libtempo.{a,lib}  # static archive — wrapper + protoc outputs
└── proto/
    └── <Package>/<File>.proto       # decorated source protos (informational)
```

## Integration

1. Add to your include path:
   - `<this-folder>/include`
   - `<this-folder>/include/proto`
2. Link against `lib/<Platform>/libtempo.{a,lib}` for your build host.
3. **Bring your own gRPC++ and protobuf** — `libtempo` does *not* bundle them. Three reasonable
   options:

    | Option | Notes |
    |---|---|
    | **Vendored from Tempo** (recommended) | Guaranteed ABI match. Use the headers and static archives at `Plugins/Tempo/TempoCore/Source/ThirdParty/gRPC/{Includes,Libraries/<Platform>}`. |
    | **vcpkg / Conan** | ABI compatibility depends on flags. |
    | **System gRPC** (`apt install libgrpc++-dev`) | Same caveat. |

4. Define the same gRPC compile flags this library was built with:

    ```text
    GOOGLE_PROTOBUF_NO_RTTI=1
    GRPC_ALLOW_EXCEPTIONS=0
    ```

## Quick start

```cpp
#include <tempo.h>

int main() {
    tempo::set_server("localhost", 10001);

    auto state = tempo::tempo_world::get_current_actor_state("MyActor");
    if (!state) {
        return 1;  // Result<T> carries a TempoError
    }
}
```

Function names match the Python and Rust clients — `pascal_to_snake` of the RPC name, namespaced
by module under `tempo::`.

## Requirements

- **CMake** 3.20 or later (for the shipped examples).
- **A C++20 compiler** — clang 14+, gcc 11+, MSVC 19.30+. Required because the vendored absl
  headers depend on `std::weak_ordering`.
- **OpenSSL 1.1** and **zlib**.

!!! warning "OpenSSL 1.x, not 3.x"

    The vendored gRPC was compiled against OpenSSL 1.x and references `SSL_get_peer_certificate`,
    which OpenSSL 3.x replaced with `SSL_get1_peer_certificate`. On macOS:
    `brew install openssl@1.1`. On Linux, point `OPENSSL_ROOT_DIR` at an OpenSSL 1.1 install, or
    rebuild gRPC against your system OpenSSL.

## Example clients

`ExampleClients/cpp/` contains three small programs that exercise the wrapper API — they log to
stdout, with no GUI and no interactive prompts.

| Example | What it does |
|---|---|
| `sensor_playground` | Lists available sensors. If a lidar exists, streams 5 scans and logs basic stats. |
| `world_playground` | Looks up an actor by name and prints its transform, velocity, and bounds. |
| `movement_playground` | Lists commandable and navigable pawns, then sends one of each movement command against the first pawn. |

```bash
cd Plugins/Tempo/ExampleClients/cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@1.1
cmake --build build --parallel
```

Each example takes optional positional arguments; defaults connect to `localhost:10001`.

```bash
./build/sensor_playground                     # localhost:10001, 5 frames
./build/sensor_playground localhost 10001 20  # stream 20 frames
./build/world_playground localhost 10001 PlayerActor
./build/movement_playground
```

!!! note "Video decode is not provided in C++"

    Python (PyAV) and Rust (`ffmpeg-next`) example clients decode the H.264 video stream. The C++
    client can subscribe to it, but no decode example ships yet.

## API reference

[:octicons-arrow-right-24: Every RPC, by module](../reference/api/index.md)
