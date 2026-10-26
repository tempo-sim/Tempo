# C++ Client

A C++ client library, generated at build time when `TEMPO_GEN_CPP_API=1`, and shipped
self-contained by `Scripts/Package.sh` with the same variable set.

```bash
export TEMPO_GEN_CPP_API=1
Plugins/Tempo/Scripts/Package.sh
```

## What you get

```text
Packaged/API/Cpp/
├── TempoConfig.cmake               # find_package(Tempo CONFIG)
├── include/
│   ├── tempo.h                     # umbrella header for the RPC client
│   ├── tempo/                      # context, error, result, streaming, one header per service,
│   │   │                           #   and unreal.h, world.h, conversion.h for the reflected API
│   │   └── reflected/              # the reflected API: one header per class, one per module
│   └── proto/                      # protoc-generated message types
├── lib/<Platform>/
│   ├── libtempo.a                  # the RPC client and the typed runtime (tempo.lib on Windows)
│   ├── libtempo_reflected.a        # the reflected API (tempo_reflected.lib on Windows)
│   └── libtempogrpc.{dylib,so} / tempogrpc.{dll,lib}
└── third_party/grpc/include/       # the vendored gRPC, protobuf and abseil headers
```

Nothing else is needed: the shared `tempogrpc` library carries gRPC, protobuf, abseil, OpenSSL and
zlib inside it, as it does for Tempo's own modules. Linking it rather than the static archives
beside it is the same rule they follow, for the same reason — one copy of gRPC's global state — and
it is a seventh of their size.

The build also leaves the same client in this checkout at `Plugins/Tempo/TempoCore/Content/Cpp/API/`,
where it finds the vendored gRPC next door. `Build.sh` alone leaves it without the reflected API,
which needs the packaged build; `Package.sh` generates it (`Scripts/GenReflectedAPI.sh`) and installs
`libtempo_reflected` and `include/tempo/reflected/` there as well as into `Packaged/API/Cpp`, so after
a package both copies carry them.

## Integration

Through CMake, against either:

```cmake
find_package(Tempo CONFIG REQUIRED PATHS <project>/Packaged/API/Cpp)
target_link_libraries(my_client PRIVATE tempo::reflected)   # the reflected API, and everything
# or
target_link_libraries(my_client PRIVATE tempo::tempo)       # the RPC client alone
```

The targets carry the include paths, the defines the library was built with
(`GOOGLE_PROTOBUF_NO_RTTI`, `GRPC_ALLOW_EXCEPTIONS=0`, `PROTOBUF_USE_DLLS`, `ABSL_CONSUME_DLL` and
the rest), `-fno-rtti` (the vendored protobuf ships no typeinfo for its message base classes), and
C++20. On Windows, copy `tempogrpc.dll` next to the executable. Without CMake, mirror what
`TempoConfig.cmake` sets.

## Quick start

```cpp
#include <tempo.h>

int main() {
    tempo::set_server("localhost", 10001);

    auto state = tempo::tempo_world::get_current_actor_state({.actor = "MyActor"});
    if (!state) {
        return 1;  // Result<T> carries a TempoError; nothing here throws
    }
}
```

Function names match the Python and Rust clients — `pascal_to_snake` of the RPC name, namespaced
by module under `tempo::`.

Each function takes one argument, a generated aggregate named after its request message with
`Request` replaced by `Args` — `ActorStateArgs` for `ActorStateRequest` — with one member per field
and a default for every one. Name the members you set with C++20 designated initializers, in the
order the struct declares them, and leave the rest out: a field added in a later release then gets
its default rather than breaking the call. A message-typed member is a `std::optional`, so leaving
it out leaves the field unset. An RPC whose request is `TempoCore.Empty` takes no argument, and a
deprecated RPC's function is marked `[[deprecated]]`. The fluent `call()` builder is the exception,
as in the other clients: `call(actor, component, function)`, then one `*_arg` method per argument
(see [TempoWorld](../plugins/tempo-world.md#calling-functions)).

## The reflected API

The same [reflected API](reflected-api.md) the Python and Rust clients have, for the project's whole
build, as `tempo::reflected`. The C++ surface mirrors the Rust one: the same snake_case names, a `Result`
from every call, and Unreal's single inheritance as C++ inheritance.

```cpp
#include <tempo/world.h>
#include <tempo/reflected/classes/engine/StaticMeshActor.h>
#include <tempo/reflected/structs.h>

using tempo::reflected::classes::engine::StaticMeshActor;

auto actors = tempo::world::get_actors<StaticMeshActor>();   // Result<std::vector<StaticMeshActor>>
if (!actors) return 1;
for (const StaticMeshActor& actor : actors.value()) {
    if (auto set = actor.set_b_can_be_damaged(false); !set) return 1;   // declared on Actor, inherited
    auto moved = actor.k2_set_actor_location({0, 0, 500}, false, true);
    if (!moved) return 1;
    auto [returned, hit] = moved.value();                     // the return value, then the out parameter
}
```

`Result` is `[[nodiscard]]`, so a setter called as a statement is a compiler warning; cast one whose
outcome really does not matter to `void`.

Include the header of each class you use (`tempo/reflected/classes/<module>/<Class>.h`), or a
module's (`tempo/reflected/engine.h`). A header declares and forward-declares only, so calling a
method that returns a class you have not included is an incomplete-type error naming it. Struct
values need `tempo/reflected/structs.h`.

`tempo::world` mirrors `tempo::tempo_world` with handles. Looking an actor up by name is
`world::get_actor<T>(name)`, which checks that it really is a `T`; `world::find_actor(name)` is the
untyped counterpart of Python's `get_actor(name)` without a class -- an `ObjectRef` whose every
property is addressed by string and checked only by the server, kept as a separate function so that
taking the escape hatch is visible where it is taken. `tempo::conversion` converts Unreal's values to
and from SI (`ue_to_si`, `si_to_ue`). Async twins are the one thing the Rust surface has
that this does not: the C++ client is synchronous throughout.

## Requirements

- **CMake** 3.20 or later.
- **A C++20 compiler** — clang 14+, gcc 11+, MSVC 19.30+. Required because the vendored absl
  headers depend on `std::weak_ordering`, and because the reflected API uses concepts.

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
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release        # or -DTempo_DIR=<project>/Packaged/API/Cpp
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
