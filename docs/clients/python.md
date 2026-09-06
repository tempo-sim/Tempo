# Python Client

The Python client is generated on every build — you don't opt in, and you don't install anything.

## Where it comes from

Tempo generates both the Python classes for the messages and services in your Protobuf files
*and* a wrapper library that makes writing client code easy.

- Tempo's own modules end up in the publishable **`tempo_sim`** package (PyPI dist name
  `tempo-sim`, import name `tempo_sim`).
- Your project's own service modules end up in a **separate project package** (e.g.
  `tempo_sample`) that depends on `tempo_sim` and re-exports its runtime helpers.

Both are installed automatically into a virtual environment at `<project_root>/TempoEnv`.

```bash
# Linux / macOS
source <project_root>/TempoEnv/bin/activate

# Windows
source <project_root>/TempoEnv/Scripts/activate
```

Alternatively, install them into your system Python or another virtual environment:

```bash
pip install "<project_root>/Plugins/Tempo/TempoCore/Content/Python/API"
# And, for the project package, if present:
pip install "<project_root>/Content/Python/API"
```

## The shape of the API

The wrapper library adds a Python module for every C++ module in the project, with synchronous and
asynchronous wrappers for every RPC in that module. Request parameters are laid out flat as
arguments.

Given this service:

```proto
service MyService {
  rpc MyRPC(MyRequest) returns (MyResponse);
}

message MyRequest {
  int32 some_request = 1;
}
```

you get:

```python
import tempo_sim.my_module as t_mm

t_mm.my_rpc(some_request=3)          # synchronous
await t_mm.my_rpc(some_request=3)    # asynchronous
```

The two have exactly the same signature; the correct one is deduced automatically based on
whether it is called from a synchronous or asynchronous context.

Generated protobuf modules nest under their owning package namespace:

```python
import tempo_sim.TempoCore.Geometry_pb2 as Geometry     # Tempo's protos
import tempo_sample.Greeter.GreeterService_pb2 as gs    # your project's protos
```

The convention is `<package>/ModuleName/RelativePath/FileName_pb2.py`.

!!! note "Proto packages have no effect in Python"

    Python has no namespaces in the C++ sense, so proto `package` declarations don't shape the
    generated Python. Messages with the same name are distinguished by the module they're
    imported from.

## Streaming RPCs

A server-streaming RPC returns an iterator (or async iterator):

```python
import tempo_sim.tempo_world as tw

for state in tw.stream_actor_state(actor="MyActor"):
    print(state.transform.location)
```

```python
async for state in tw.stream_actor_state(actor="MyActor"):
    ...
```

## Helpers

| Helper | What it does |
|---|---|
| `tempo_sim.set_server(address=..., port=...)` | Point the client at a server. See [Connecting](connecting.md). |
| `tempo_sim.run_async(...)` | Run a coroutine from synchronous code. |
| `tempo_sim.TempoImageUtils` | Stream and decode color / depth / label / video frames. |
| `tempo_sim.TempoLidarUtils` | Decode lidar scan segments into point clouds. |

The image and lidar helpers already use `np.frombuffer` on the `bytes` payloads, so if you use
them you get the fast path for free — see the
[sensor payload migration](../migration/sensors-v0.2.0.md).

!!! tip "Shorter imports"

    ```python
    import tempo_sim as tempo
    from tempo_sim import tempo_world, tempo_sensors
    ```

## Publishing your own package

You don't do anything special to "create" your project package — it falls out of the build as
soon as your project defines Protobuf services. See
[Adding your own services](../guides/custom-services.md).

To share it, publish to [PyPI](https://pypi.org/). After a build with the API generated,
`Scripts/Package.sh` stages wheels and source distributions under `Packaged/API/Python/`:

```text
Packaged/API/Python/tempo-sim/        # Tempo's own services
Packaged/API/Python/<your-project>/   # your project's services, if any
```

Upload them with [twine](https://twine.readthedocs.io/) — `tempo-sim` first, since your project
package depends on it:

```bash
twine upload Packaged/API/Python/tempo-sim/*
twine upload Packaged/API/Python/<your-project>/*
```

Publish metadata for the project package lives in `Content/Python/API/project_info.json` — edit
that, **not** the generated `pyproject.toml`, which is overwritten on every build.

## Using the pre-built `tempo-sim` instead of building

If you only need a client for Tempo's **built-in** services, skip the Unreal build entirely:

```bash
pip install tempo-sim
pip install "tempo-sim[examples]"   # adds visualization / example-client dependencies
```

!!! warning

    The published `tempo-sim` contains only Tempo's own services — it does **not** include custom
    RPCs you define in your project. If your project defines its own services, generate and use
    your project package instead. Keep the installed `tempo-sim` version matched to the Tempo
    version your server runs.

## API reference

[:octicons-arrow-right-24: Every RPC, by module](../reference/api/index.md)
