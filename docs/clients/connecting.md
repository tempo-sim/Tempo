# Connecting to a Server

By default a Tempo client connects to a Tempo server — an Unreal Editor or packaged binary using
Tempo — on the same machine (`localhost`), at port **10001**. On Linux and macOS it can use a
[Unix domain socket](#several-sims-on-one-machine-without-ports) instead, which is the easier way
to run several sims at once.

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

## Several sims on one machine, without ports

On Linux and macOS the server can listen on a **Unix domain socket** instead of a TCP port. The
socket is a file, so each sim gets a *name* rather than a number — no hunting for a free port, and
no risk of colliding with an unrelated service. The transports are exclusive: a server listens on
one or the other, never both, and a socket is reachable only from the same machine.

Start the sim on a socket via `Project Settings → Plugins → Tempo Core → Server → Server
Transport`, or on the command line:

```bash
MyGame.sh -ServerSocket=sim-a.sock
MyGame.sh -ServerSocket=/run/my-fleet/sim-a.sock   # or give it a full path
```

`-ServerSocket=` selects the socket transport, the same way `-ServerPort=` selects TCP. The log
confirms the endpoint:

```text
LogTempoCore: Display: Tempo gRPC server listening on unix:/run/user/1000/tempo/sim-a.sock
```

Point the client at the same name:

=== "Python"

    ```python
    import tempo_sim
    tempo_sim.set_socket("sim-a.sock")

    # Or in an async context, including Jupyter notebooks:
    await tempo_sim.set_socket("sim-a.sock")
    ```

=== "Rust"

    ```rust
    use tempo_sim::{set_socket, set_socket_async};

    set_socket("sim-a.sock");
    set_socket_async("sim-a.sock").await;
    ```

=== "C++"

    ```cpp
    #include <tempo.h>

    tempo::set_socket("sim-a.sock");
    ```

### How a socket name is resolved

A **bare name** like `sim-a.sock` lands in a short, user-private directory —
`$XDG_RUNTIME_DIR/tempo` on Linux, `/tmp/tempo-<uid>` on macOS. Server and clients apply the same
rule, so the name alone is enough to meet. Anything that looks like a path is used as a path, and an
empty value means `tempo.sock` in that same directory — the counterpart of the default port.

The per-user directory matters for two reasons: a socket's own permissions come from the process
umask, so access control lives on the directory (Tempo creates it `0700`), and a socket **address**
cannot hold a path longer than about 104 bytes, which a project's `Saved` directory can easily
exceed.

On Linux that directory depends on `$XDG_RUNTIME_DIR`, so a sim and a client launched from
different environments — a desktop session versus a service or container — can read the same bare
name as two different files. The server logs the path it actually bound; if a bare name doesn't
connect, compare that line with where the client is looking, or give both sides a full path.

### One variable for both sides

A client reads its endpoint from the environment at startup, so a launcher can point a sim and its
client at the same socket without either of them changing code:

```bash
export TEMPO_SERVER_SOCKET=run-7.sock
MyGame.sh -ServerSocket="$TEMPO_SERVER_SOCKET" &
python my_client.py          # already connects to run-7.sock
```

`TEMPO_SERVER_SOCKET` takes precedence over `TEMPO_SERVER_ADDRESS` and `TEMPO_SERVER_PORT`, which
work the same way for the TCP transport. An explicit `set_socket()` or `set_server()` call always
overrides the environment.

!!! warning "Sockets do not cross machines or containers"

    A Unix domain socket only reaches processes that can see the file. A client in a container needs
    the socket's directory bind-mounted (and a matching uid); a client on another machine needs TCP.
    Windows has no support for this transport in Tempo's gRPC build — use a port there.

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
