# Benchmarks

Measurements to run against a live, rendering sim. Not part of any test group or CI job.

## `write_wait.py`: what sensor writes cost a frame

Mounts a rig (default: eight 1920x1080 cameras and a 128 x 2048 lidar, all at 10 Hz) on one actor,
streams every sensor to the script, and reads the server's write stats over a measurement window.

```bash
# Against a sim on the default endpoint (or TEMPO_SERVER_ADDRESS/PORT/SOCKET)
python Tests/Benchmarks/write_wait.py
python Tests/Benchmarks/write_wait.py --measurements color,depth,label --json all.json
python Tests/Benchmarks/write_wait.py --width 960 --height 540
```

In FixedStep (the default), the sim plays as fast as it can with one capture per frame, and before
time moves on it waits for every write in flight to complete. That wait is what the report calls
**write wait**. It is added to every frame, so it is the time a faster transport could save. The
frame rate divided by `--rate` is how far past real time the rig runs.

The figures come from the server, under `Tempo.WriteStats`. Any client can read them:

| Console | Does |
|---|---|
| `Tempo.WriteStats 1` | starts recording (a new window); `0` stops |
| `Tempo.WriteStats.Dump [reset]` | prints the window as one line of JSON; `reset` also starts a new one |
| `Tempo.WriteStats.Reset` | starts a new window |

`frame_wait_ms` is per game frame. `write_latency_ms` runs from the moment a write is issued until
the client's flow-control window has room for it. `write_call_ms` is the time inside gRPC's `Write`,
which serializes the message. `queued_responses` counts responses that arrived while a write was
still in flight on their stream and were copied into its queue.

Only the measurement window is counted, so measure with the world playing: paused frames count as
frames too. The client counts only bytes and never decodes, so it is the best case for a Python
client.
