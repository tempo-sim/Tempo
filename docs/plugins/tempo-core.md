# TempoCore

TempoCore hosts time control, the gRPC server, and the core utilities most any simulation
application needs. Every other Tempo plugin depends on it.

## Time

Tempo supports two time modes — `Wall Clock` and `Fixed Step` — and overrides Unreal's engine
time to make each one mean exactly what it says.

[:octicons-arrow-right-24: Time, in depth](../concepts/time.md)

The time service exposes them over the API:

```python
import tempo_sim.tempo_core as tc
import tempo_sim.TempoCore.Time_pb2 as Time

tc.pause()
tc.set_time_mode(Time.TM_FIXED_STEP)
tc.set_sim_steps_per_second(10)
while True:
    tc.step()
    # ... do some great simulation
```

## Simulation lifecycle

The core service manages levels and process lifetime.

There are cases where you want to load a level and set properties on its actors *before*
`BeginPlay` runs — for instance a property that will be read during `BeginPlay`. For that, level
loads can be **deferred**: every actor in the level is loaded, but `BeginPlay` is not called yet.

```python
import tempo_sim.tempo_core as tc

tc.load_level("MyLevel", deferred=True)
# ... set properties on the actors in MyLevel (see TempoWorld)
tc.finish_loading_level()
# ... do some great simulation
tc.quit()
```

`get_available_levels()` and `get_current_level_name()` round out the set.

### Runtime knobs

| RPC | Effect |
|---|---|
| `set_main_viewport_render_enabled` | Turn the main viewport's rendering off to save GPU when only sensors matter. |
| `set_engine_scalability` | Change Unreal's scalability settings at runtime. |
| `set_control_mode` | Switch the control mode. |
| `get_sim_time` | Read the current simulation time, in seconds. |

`advance_steps(steps=N)` advances several fixed steps in one call.

!!! note "One step at a time"

    Only one `advance_steps` / `step` RPC may be in flight at a time. If you need to do other work
    while waiting, use the asynchronous client API.

### Editor-only control

`tempo_core_editor` exposes the Editor's own lifecycle, which is what test harnesses and content
pipelines drive:

| RPC | Effect |
|---|---|
| `play_in_editor` | Start PIE. |
| `simulate` | Start Simulate In Editor. |
| `stop` | Stop PIE or Simulate. |
| `open_level` / `new_level` / `save_level` | Open, create, and save levels in the Editor. |

## Default HUD

![The Tempo HUD running in TempoSample](https://github.com/user-attachments/assets/41ece0a4-b18a-47c9-bf00-07b5733987b4){ loading=lazy }
/// caption
The default Tempo HUD, running in the TempoSample Lower Sector level.
///

TempoCore ships a HUD that lets a human at the keyboard control time and the rest of the
simulation. Use it by adding a `Tempo_HUDAdvanced` to your viewport — for example in your game
mode. TempoSample does exactly this in `TempoSampleGameMode_BP`, so you can see it in place.

![The HUD's time controls](https://github.com/user-attachments/assets/f9436705-b159-43ab-8538-5bee56981374){ loading=lazy }

### Exposed bindings

![The bindings widget](https://github.com/user-attachments/assets/3ee1ae4d-bc8c-4f03-b79a-3fa1cdaeb67f){ loading=lazy }

The bindings widget lets you rebind existing functions to new keys: click the binding you want to
change and press the new key. Any *new functions* you create and add to the **Project Settings**
are found and populated into the scrollable list automatically.

### Possessing, creating and destroying pawns

![The possessable actors widget](https://github.com/user-attachments/assets/fafe6c78-8341-4087-878a-51898fb730b2){ loading=lazy }

The possessable actors widget holds every possessable actor in the scene. In TempoSample that is
a spectator pawn, a street sweeper, and several "block bots". Possess any of them by left-clicking
the pawn itself or its entry in the widget. Middle-click and right-click create and destroy block
bots respectively. When you unpossess a pawn, the controller that previously possessed it takes it
back automatically.

Hovering over a possessable pawn in the viewport shows a cursor icon:

![The possessable cursor icon](https://github.com/user-attachments/assets/398d13ed-f1e4-440c-8a06-bf0453605953){ loading=lazy }

!!! note "Pawn groups"

    Pawns are grouped. The default `Possess Next` / `Possess Previous` bindings move within a
    group; `Switch Group` moves between them. Edit the grouping in `TempoPlayerController_BP` →
    **Class Defaults**. Lowest index has highest priority.

    ![The default pawn groupings](https://github.com/user-attachments/assets/f6bff8e0-450f-4c5b-8747-1fa3ada6b0b5){ loading=lazy }

Turning on **Highlight Possessed Pawn** draws a debug point above your current pawn for easy
tracking.

### Hiding elements

![The category visibility widget](https://github.com/user-attachments/assets/0296c4bc-eaf4-4d04-bd1d-662a31649898){ loading=lazy }

Individual widgets can be hidden by toggling categories in the hover widget. To hide everything
for a clean display, enter **Immersive Mode** — bound to ++0++ by default.

## The gRPC server

TempoCore hosts a single gRPC server (`FTempoServer`) per Unreal process, defined via
[Protobuf](https://protobuf.dev/) and [gRPC](https://grpc.io/). Every plugin's services — and
your project's — register with that one server, so clients open one connection and reach
everything.

On startup the log confirms it:

```text
LogTempoCore: Display: Tempo gRPC server listening on 0.0.0.0:10001
```

Adding your own services to it is a first-class workflow, not a fork:

[:octicons-arrow-right-24: Adding your own services](../guides/custom-services.md)

And connecting a client to it — including from another machine, or to several sims at once — is
covered in:

[:octicons-arrow-right-24: Connecting to a server](../clients/connecting.md)

!!! tip "A minimal, readable example"

    The [Greeter](https://github.com/tempo-sim/Greeter/) plugin is a bare-bones demonstration of
    adding and hooking up a simple RPC. It is included in TempoSample, so you can build it, call
    `tempo_sample.greeter.greet(message="hi")`, and watch a debug sphere appear.

## Utilities

- **`ATempoWorldSettings`** extends `AWorldSettings` with Tempo's time modes and rendering
  settings, including a "Default Exposure Compensation" helper that derives a sensible manual
  exposure from the level's directional light.
- **`UTempoCoreUtils::GetActorIdentifier`** produces the stable actor identifier Tempo's own RPCs
  report. Use it in your own services rather than `GetActorNameOrLabel` — see
  [Naming](../concepts/naming.md).
- **`TempoSpectatorPawn`**, **`TempoPlayerController`** and **`TempoGameMode`** are the base
  classes TempoSample's Blueprints derive from.

## Settings

See the [settings reference](../reference/settings.md#tempo-core) for time, server, packaging,
rendering and control settings.

## API reference

[:octicons-arrow-right-24: `TempoCore` RPCs](../reference/api/tempo-core.md) &nbsp;·&nbsp;
[:octicons-arrow-right-24: `TempoCoreEditor` RPCs](../reference/api/tempo-core-editor.md)
