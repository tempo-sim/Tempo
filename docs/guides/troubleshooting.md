# Troubleshooting

## Build and setup

### Engine mods or third-party deps are out of sync

If you set up with `Setup.sh -skip-hooks`, nothing re-syncs automatically when you change Tempo
commits. Run them yourself:

```bash
Plugins/Tempo/Scripts/InstallEngineMods.sh
Plugins/Tempo/Scripts/SyncDeps.sh
```

### `GenROSIDL` prebuild fails with a `TypeError` from `em.py`

```text
TypeError: '>' not supported between instances of 'str' and 'int'
```

A known, intermittent TempoROS issue — still being debugged. For whatever reason it seems more
likely to happen over SSH. Re-running the build usually gets past it.

## Connecting

### The client can't reach the server

Check the sim's log for:

```text
LogTempoCore: Display: Tempo gRPC server listening on 0.0.0.0:10001
```

If instead you see:

```text
LogTempoCore: Error: Error while starting Tempo gRPC server. Perhaps port 10001 was not available.
```

something else already holds the port — often another Tempo instance. Give one of them a different
port with `-ServerPort=10002`, and point the client at it.

[:octicons-arrow-right-24: Connecting to a server](../clients/connecting.md)

### An async stream dies when I call the API from another thread

The gRPC channel is bound to the event loop that created it. Calling the **synchronous**
`tempo_sim` API from a different thread rebuilds the channel and kills any async streams running
on it.

Marshal calls onto the loop that owns the channel — `asyncio.run_coroutine_threadsafe(...)` —
instead of calling the sync API from the other thread. `tempo_panel.py`'s `AsyncBridge` in the
example clients is a working pattern.

### Everything works in Jupyter except the sync calls

All notebook code runs in an `async` context, so you must always `await` Tempo calls there.

## World control

### `set_actor_transform` does nothing

Since v0.3.0 the transform RPCs are **partial updates**, and presence — not value — decides what
gets applied. `set_actor_transform(actor="X", transform=Geometry.Transform())` supplies nothing
and is therefore a no-op. It used to reset the actor to the origin.

Spell the reset out if you meant it, or use `set_actor_location` / `set_actor_rotation`, which are
not partial.

[:octicons-arrow-right-24: Migrating to v0.3.0](../migration/v0.3.0.md)

### My actor name works in the Editor but not in the packaged build

Packaged actors get unique names that are not the Editor labels. Query for actors and use the
names from the responses.

[:octicons-arrow-right-24: Naming](../concepts/naming.md#actors)

### A property set is silently in the wrong units

`set_float_property` and the vector-shaped setters are **not** unit-converted — they write the raw
`UPROPERTY`, so distances must be in centimeters. Rotators, quats and transforms *are* converted.

[:octicons-arrow-right-24: Units and coordinates](../concepts/conventions.md#the-exception-set_property)

## Sensors

### Lidar or depth values decode as garbage

Since API v0.2.0 the per-return and per-pixel arrays are `bytes`, not repeated numerics. Use
`np.frombuffer`, not `np.asarray`.

`labels` is the dangerous one: old varint-encoded `repeated uint32` and new fixed 4-byte
little-endian `bytes` share a wire type, so an old client decodes garbage from a new server with
no error raised.

[:octicons-arrow-right-24: Sensor payload migration](../migration/sensors-v0.2.0.md)

### Frames are being dropped with a warning

`Project Settings → Tempo → Sensors → Max Camera Render Buffer Size` (default 4) caps how far a
sensor can fall behind. Captures past that are skipped with a warning. Either lower the sensor's
`RateHz`, or enable `Pipelined Rendering` to let game / render / readback run in parallel.

### A property change on a streaming sensor doesn't take effect

Reconfigures are deferred until in-flight reads have drained, so they never tear mid-frame. The
change applies once the queue empties.

### The sim crashes during ray tracing with many scene captures

TempoSensors ships a workaround for an `FRayTracingScene` engine bug, on by default
(`bEnableRayTracingSceneReadbackBuffersOverrunWorkaround`). If you disabled it, re-enable it. See
the [settings reference](../reference/settings.md#tempo-sensors).

## Packaging and CI

### A cold CI package crashes

Pass `-skipiostore`. On a clean run iostore can't find engine content under `Saved/Temp`. Note
that iostore is enabled by `bUseIoStore` in `Config/DefaultGame.ini`, not by an `-iostore` flag.

### A packaged game with TempoROS won't start on Windows

Add this to your `PATH`:

```text
<package_root>/<YourProjectName>/Plugins/Tempo/TempoROS/Source/ThirdParty/rclcpp/Binaries/Windows
```

## Still stuck?

Find us on [:fontawesome-brands-discord: Discord](https://discord.gg/bKa2hnGYnw), or open an
[issue](https://github.com/tempo-sim/Tempo/issues).
