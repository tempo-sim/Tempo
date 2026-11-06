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
| `advance_steps(steps=N)` | Advance `N` steps. |

`step()` and `advance_steps()` report where they left the [game frame](#game-frames):
`game_frame` is whether the last step completed one, and `steps_until_game_frame` is how many steps
remain until the next. With the default of one step per game frame, every step is a game frame.

In the Editor, Tempo's pause and Unreal's own PIE pause are kept in sync, and the "Advance Single
Frame" toolbar button is wired to Tempo's stepping, advancing to the end of the game frame — so the
Editor controls and the API controls agree rather than fighting each other.

## Game frames

By default every Fixed Step step is a frame: the world ticks once per step. `StepsPerGameFrame`
groups steps into **game frames** instead, so the world ticks once every `StepsPerGameFrame` steps,
over all of them. Steps keep their length, `1 / sim_steps_per_second`, and a client keeps calling
`step()`. Game frames are Fixed Step only: in Wall Clock mode every frame is one step, whatever
`StepsPerGameFrame` says. Game frames are for whatever must advance more finely than the world ticks, such as a
physics plugin, and for a client that needs to act between those finer steps.

```python
tc.set_time_mode(Time.TM_FIXED_STEP)
tc.set_sim_steps_per_second(100)
tc.set_steps_per_game_frame(10)    # the world ticks at 10 Hz
tc.pause()

while True:
    response = tc.step()
    # ... sim time has advanced 0.01 s; read state and send commands for the next step
    if response.game_frame:
        ...  # the world has just ticked: actors moved, sensors captured
```

A game frame ticks every `StepsPerGameFrame` steps, counted from where the setting last changed,
however the client advances them and however long it takes. A step that doesn't complete a game
frame runs between frames:

- **Sim time moves.** `get_sim_time` and every timestamp the server reports (actor state, sensor
  transmission time) give the latest step's time.
- **Nothing else does.** Actors don't tick, timers don't fire, and sensors don't capture until
  the game frame ticks, over all of its steps at once. Code that must run every step subscribes to
  `ATempoWorldSettings::OnStep`. It fires in Wall Clock mode too, once per frame, with the frame's
  measured length.
- **Earlier captures still arrive.** With `Pipelined Rendering`, a frame's measurements are read
  back after the frame, so they reach the client between the next game frame's steps as soon as
  the GPU has finished them.
- **Some RPCs need a game frame.** Time settings, level loads and console commands run the rest of
  the game frame's steps and are handled once it has ticked.

Sensors capture only when the world ticks, so their rates are rounded to game frames. In Fixed Step
mode a sensor's captures fall on multiples of its period in sim time, so every sensor at a rate
captures on the same steps, in every run. The one exception is the first capture after a sensor's
timer starts outside a frame, as it does on a rate change or a time-mode switch while paused: that
one can come a game frame late, and the captures after it are on the grid.

!!! note "Unreal timers on the Fixed Step grid"

    `FTimerManager` fires a timer only once it is strictly overdue, and its clock and a timer's
    expiries are sums of the same floats. So in Fixed Step a timer started on a frame boundary, whose
    period is a whole number of frames, is exactly due on every later frame rather than overdue,
    and fires a frame late each time: with a period of one frame, every other frame. Tempo's sensors
    start their timers due the float just below their first period (`std::nextafter`), which breaks
    that tie exactly; a timer of your own that should fire on the frame grid needs the same.

!!! warning "Chaos only follows steps through its own settings"

    Tempo can't step Chaos between frames, so Chaos integrates each game frame in its own
    substeps, from the physics settings' `bSubstepping`, `MaxSubstepDeltaTime` and `MaxSubsteps`.
    Tempo warns when those are coarser than its steps or would drop time. Async physics
    (`bTickPhysicsAsync`) runs on its own clock, so Tempo ignores `StepsPerGameFrame` when it's on.

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
`SimulatedStepsPerSecond`, `StepsPerGameFrame`, and `MaxWallClockTimeStep`.
