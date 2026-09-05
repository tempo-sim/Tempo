# Time

Tempo supports two time modes: **Wall Clock** and **Fixed Step**. Both override Unreal's engine
time, because the engine does not on its own provide the guarantees a simulator needs.

Switch between them from a client, or set the default in
`Project Settings → Plugins → Tempo Core → Time`.

```python
import tempo_sim.tempo_core as tc
import tempo_sim.TempoCore.Time_pb2 as Time

tc.set_time_mode(Time.TM_WALL_CLOCK)
tc.set_time_mode(Time.TM_FIXED_STEP)
```

## Wall Clock mode

Time advances *strictly* alongside the system clock. Tempo overrides Unreal's engine time to
guarantee this.

The cost is that a slow task on the game thread produces a correspondingly large time step. If
you want sim time to follow the system clock *except* when doing so would cause a large step, set
`MaxWallClockTimeStep` to a non-zero value — accepting that this violates the strict guarantee at
exactly the step that would have exceeded the maximum.

Use Wall Clock when you are driving interactively, or when your client is a real-time system that
must keep up.

## Fixed Step mode

Time advances by a fixed amount every frame, regardless of how long the frame actually took.

The increment is expressed as a whole number of **simulated steps per second** (10 steps per
second), not as a floating-point fraction of a second (0.1 seconds per step). That is deliberate:
Tempo uses a fixed-point representation for time — again, overriding the engine's — because sim
time must be exactly correct, with no rounding or floating-point drift over a long run.

```python
tc.set_time_mode(Time.TM_FIXED_STEP)
tc.set_sim_steps_per_second(10)
tc.pause()

while True:
    tc.step()
    # ... read sensors, apply commands, and know exactly how much time passed
```

Use Fixed Step for reproducible runs, dataset generation, and any test that asserts on timing.

!!! note "The first step after switching is a partial one"

    When you switch into Fixed Step mode, sim time keeps whatever value it had — some arbitrary
    time accumulated in Wall Clock mode — and snaps onto the step grid on the first step. So the
    *first* `step` after switching advances only by the partial amount needed to reach the next
    multiple of `1 / sim_steps_per_second`; every step after that advances by exactly
    `1 / sim_steps_per_second`.

    If you need the first step to be a full step — for example when asserting on exact per-step
    time deltas — take one throwaway step right after switching, to align sim time to the grid.

## Pausing, playing, and stepping

| RPC | Effect |
|---|---|
| `pause()` | Stop advancing sim time. |
| `play()` | Resume advancing sim time. |
| `step()` | Advance one step (Fixed Step mode). |

In the Editor, Tempo's pause and Unreal's own PIE pause are kept in sync, and the "Advance Single
Frame" toolbar button is wired to Tempo's `step` — so the Editor controls and the API controls
agree rather than fighting each other.

## Why this matters downstream

Fixed Step mode is what makes the rest of the API deterministic:

- **Server events.** In Fixed Step mode, Tempo processes *all* received gRPC events every tick,
  rather than budgeting a fixed number of microseconds per tick. A command sent before a step is
  applied in that step.
- **Sensors.** By default, in Fixed Step mode the game thread blocks until each frame's sensor
  data is ready, so a client receives data for the simulation frame it was captured in. Setting
  `Pipelined Rendering` trades that for throughput — see
  [TempoSensors](../plugins/tempo-sensors.md). Either way, each measurement carries the
  `capture_time_s` and `sequence_id` of the frame that produced it.
- **Trajectories.** Trajectory time accumulates from the simulation frame delta, so it holds
  steady while the sim is paused or between fixed steps — see
  [TempoMovement](../plugins/tempo-movement.md).

## Settings

See the [settings reference](../reference/settings.md#tempo-core) for `TimeMode`,
`SimulatedStepsPerSecond`, and `MaxWallClockTimeStep`.
