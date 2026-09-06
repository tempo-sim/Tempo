# Connecting to a Server

By default a Tempo client connects to a Tempo server — an Unreal Editor or packaged binary using
Tempo — on the same machine (`localhost`), at port **10001**.

## A different machine

The client and server need not be on the same machine.

=== "Python"

    ```python
    import tempo_sim
    tempo_sim.set_server(address="my_server_ip")

    # Or in an async context, including Jupyter notebooks:
    await tempo_sim.set_server(address="my_server_ip")
    ```

=== "Rust"

    ```rust
    use tempo_sim::{set_server, set_server_async};

    set_server("my_server_ip", 10001);
    set_server_async("my_server_ip", 10001).await;
    ```

=== "C++"

    ```cpp
    #include <tempo.h>

    tempo::set_server("my_server_ip", 10001);
    ```

## A different port

You can run several Tempo servers on one machine by giving each a non-default port — via
`Project Settings → Plugins → Tempo Core → Server → Server Port`, or on the command line:

```bash
$UNREAL_ENGINE_PATH/Engine/Binaries/<PLATFORM>/UnrealEditor -ServerPort=10002
```

```bash
MyGame.sh   # or .exe / .app
MyGame.sh -ServerPort=10002
```

The command-line value takes precedence over project settings, until project settings are modified
during an Editor session.

Point the client at it:

```python
import tempo_sim
tempo_sim.set_server(port=10002)

# Or in an async context:
await tempo_sim.set_server(port=10002)
```

## Compression

`Server Compression Level` controls how Tempo compresses server messages. When the client is on
the same machine, **no compression is fastest**. Across a network, compression may reduce
bandwidth enough to be worth the CPU. See the
[settings reference](../reference/settings.md#tempo-core).

## Threading, in Python

!!! warning "The gRPC channel is bound to the event loop that created it"

    Calling the synchronous `tempo_sim` API from a different thread rebuilds the channel and kills
    any async streams already running on it.

    If you need to issue calls from another thread while a stream is active, marshal them onto the
    loop that owns the channel — `asyncio.run_coroutine_threadsafe(...)` — rather than calling the
    sync API directly.

## Notebooks

The Tempo Python API works in IPython and Jupyter. All code in a Jupyter notebook runs in an
`async` context, so you must always use the **asynchronous** form there:

```python
import tempo_sim
import tempo_sim.tempo_world as tw

await tempo_sim.set_server(address="localhost")
await tw.spawn_actor(actor_type="BP_SensorRig")
```

TempoSample ships a notebook at `Content/Python/ExampleClients/TempoSimExamples.ipynb`:

```bash
source ./TempoEnv/bin/activate
pip install jupyter
jupyter lab
```

## Verifying a connection

```python
import tempo_sim.tempo_core as tc
print(tc.get_current_level_name())
```

If that raises, check that the sim is running and that its log contains:

```text
LogTempoCore: Display: Tempo gRPC server listening on 0.0.0.0:10001
```

More failure modes in [Troubleshooting](../guides/troubleshooting.md).
