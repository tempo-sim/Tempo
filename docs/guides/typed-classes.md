# Typed classes, step by step

A scenario written against the string-keyed property API fails at the line that runs, in the middle
of a simulation, if a property was renamed since it was written. The
[typed classes](../clients/typed-classes.md) move that failure to your type checker. This walks from
a fresh checkout to a scenario a checker reads, and to watching it catch a rename.

What it assumes: a packaged build, Python 3.9 or newer, and nothing else. The same classes are
generated for Rust; [step 11](#11-rust-instead-of-python) is that path, and the steps before it
apply to both.

## 1. Package the build

The classes are generated from the build they describe — walked out of that build's own Unreal
reflection — so they ship with it rather than from a release index:

```bash
./Plugins/Tempo/Scripts/Package.sh
ls Packaged/API/Python
```

```text
tempo-sample  tempo-sim
```

Two distributions, and you want both:

| Distribution | What it is |
|---|---|
| `tempo-sim` | The client: the generated gRPC wrappers, the runtime behind attribute access, `tempo_sim.world`, `tempo_sim.conversion`, and the published types for what every build has, as `tempo_sim.reflected`. Those are generated from the latest engine Tempo supports, and only that one ([Latest engine only](../clients/typed-classes.md#latest-engine-only)). |
| `tempo-sample` | The host project's own package, named after the project: the typed classes for this build, as `tempo_sample.reflected`, exact for its engine and including its Blueprints and asset catalog, and the project's own services, if it defines any. |

!!! note "`Package.sh` only"

    `Package.bat` does not build the Python wheels today, so on Windows run the packaging step
    through Git Bash.

## 2. Make a virtual environment

Keep it out of the engine's Python and out of `TempoEnv`, which the build scripts own:

```bash
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
```

## 3. Install the client and the classes

```bash
pip install Packaged/API/Python/tempo-sim/*.whl \
            Packaged/API/Python/tempo-sample/*.whl
```

Importing the classes is what registers them with the runtime, so the check is an import:

```bash
python -c "import tempo_sample.reflected; from tempo_sim import world; print('ready')"
```

Both packages are marked under [PEP 561](https://peps.python.org/pep-0561/), which is what lets a
type checker use the types they ship. Nothing further is needed to point a checker at them — being
installed in the environment is the whole configuration.

## 4. Turn a type checker on

Completion works as soon as the packages are installed. Catching drift is a separate thing: it needs
a checker that is *required to pass*. Pyright is the one to use here — on a schema this size it runs
in about a second, against roughly a minute for mypy.

```bash
pip install pyright
```

`pyrightconfig.json`, beside your scenarios:

```json
{
  "typeCheckingMode": "standard",
  "reportUnknownMemberType": "error",
  "reportUnknownArgumentType": "error",
  "reportUnnecessaryTypeIgnoreComment": "error"
}
```

Why those three on top of `standard`:

- **`reportUnknownMemberType`** and **`reportUnknownArgumentType`** — a member whose type a checker
  cannot work out is the shape drift takes when something falls out of the typed surface. Left as
  warnings, that passes quietly.
- **`reportUnnecessaryTypeIgnoreComment`** — stops a `# type: ignore` outliving the thing it
  silenced. Without it, the first rename anyone silences is silenced forever.

```bash
pyright
```

!!! warning "The string-keyed API is not typed yet"

    This covers the typed classes. The generated RPC wrappers — `tempo_sim.tempo_world`,
    `tempo_sim.tempo_core` and the rest — are dual sync/async, which a checker reads as
    coroutine-returning, so `tempo_world.get_all_actors().actors` draws an error even though it is
    correct. Until those carry stubs, keep such calls in their own module and exclude it, or
    `# type: ignore` the line.

## 5. Autocomplete in an editor

Every editor below needs the same one thing: the interpreter from step 2.

=== "VS Code"

    Install the Python extension (Pylance comes with it), then **Python: Select Interpreter** and
    pick `.venv`. To have the editor and CI agree, point Pylance at the same settings:

    ```json
    {
      "python.analysis.typeCheckingMode": "standard",
      "python.analysis.diagnosticSeverityOverrides": {
        "reportUnknownMemberType": "error",
        "reportUnknownArgumentType": "error"
      }
    }
    ```

=== "PyCharm"

    **Settings → Project → Python Interpreter → Add Local Interpreter → Existing**, and select
    `.venv/bin/python`. PyCharm reads the shipped stubs for completion, but it does not run Pyright
    — keep that in CI, or as an external tool.

=== "Neovim / other LSP"

    Point your client at `pyright-langserver --stdio` with the project root as the workspace, and
    the interpreter from step 2 on `PATH` (or set `python.pythonPath`).

## 6. Run the example

Start a sim — the packaged binary, headless, is enough:

```bash
./Packaged/Mac/TempoSample.app/Contents/MacOS/TempoSample -nullrhi -unattended -nosound
```

Then, from the project root. The example lives with the project whose classes it names — for
TempoSample that is `Content/Python/ExampleClients/TypedClasses.py`:

```bash
python ./Content/Python/ExampleClients/TypedClasses.py
```

```text
--- Spawning ---
spawned BP_StreetSweeper_C('BP_StreetSweeper_C_2147482057')

--- Properties ---
RotationRate was 60.0
RotationRate is now 120.0

--- Components ---
Body is a StaticMeshComponent
Body.RelativeLocation = UnrealVector(X=0.0, Y=0.0, Z=500.0)
steering factor 1.0

--- Functions ---
moved to UnrealVector(X=300.0, Y=0.0, Z=500.0)

--- Batching and snapshots ---
two writes, one call
RotationRate 45.0 (read from the snapshot)

--- Watching ---
  RotationRate -> 45.0
  RotationRate -> 60.0
  RotationRate -> 75.0

--- Discovery ---
2 pawn(s) in the world; our sweeper among them: True

--- Cleaning up ---
destroyed
```

The file is about 140 lines and each section stands alone. Worth knowing as you read it:

- **The class is the argument.** `world.spawn_actor(BP_StreetSweeper_C)` hands back a
  `BP_StreetSweeper_C`, and `world.get_actors(Pawn)` hands back handles typed `Pawn`. Discovery by
  type has no unchecked string in it, which a name does
  ([Naming](../concepts/naming.md) — an actor's name in a packaged build is not its editor label).
- **Components are typed all the way down.** `sweeper.Body` is a `StaticMeshComponent`, so
  `sweeper.Body.RelativeLocation` is checked too.
- **A write sets a property; it does not run what derives from it.** The example moves the sweeper by
  calling `K2_SetActorLocation`, not by assigning to a transform, because assigning never calls
  `UpdateComponentToWorld`.
- **Units are Unreal's own** — centimetres, degrees, left-handed. This is the one corner of the
  Tempo API that does not convert, which is why its types are named `UnrealVector` and so on rather
  than `Vector`, the type the other RPCs use; `tempo_sim.conversion` converts between them. See
  [Units](../clients/typed-classes.md#units).
- **`is_a` asks about the actor**, not about the handle, which is what makes narrowing a broad query
  work.

## 7. Configure a Tempo sensor

Tempo's own components are in the generated classes alongside the engine's, so a sensor is
configured the same way as anything else:

```python
from tempo_sample.reflected.TempoSensors import TempoLidar

lidar = sweeper.TempoLidar          # typed as TempoLidar

with world.batch():                 # one call, so one reconfigure
    lidar.HorizontalBeams = 1024
    lidar.VerticalBeams = 64
    lidar.HorizontalFOV = 90.0
    lidar.MaxDistance = 20000.0     # centimetres -- 200 m
```

Those take effect on the next frame. `UTempoTiledSceneCaptureComponent::TickComponent` compares each
watched parameter against its mirror and reconfigures the tiles when one has moved, and the
parameters that do not need a reconfigure are read fresh per measurement — so unlike a transform
property, writing these is enough.

The batch matters more here than it does elsewhere: four separate writes are four round trips, and
the lidar would see up to four of them on separate frames and rebuild its tiles each time.

!!! note "What a Tempo class offers is what it declares"

    A property reaches the typed classes only when it is declared `BlueprintReadWrite` (writable) or
    `BlueprintReadOnly` (read-only), and a function only when it is `BlueprintCallable` or
    `BlueprintPure`. `VisibleAnywhere` or `EditAnywhere` alone is not enough: those are for the
    editor, and a property with only them is absent. That is deliberate: the specifier is how a
    Tempo class says which of its properties are public scenario API, as against the internals that
    are `UPROPERTY` only so the garbage collector can see them. The string-keyed API still reaches
    everything, with no guarantee attached. See
    [What a class offers](../clients/typed-classes.md#what-a-class-offers).

## 8. Watch a rename get caught

This is the part worth doing by hand. Near the end of the example are four commented lines:

```python
#   sweeper.RotatoinRate = 45.0              # misspelled -- no such member
#   sweeper.Body.RelativeLocation = target    # read-only: BlueprintReadOnly in the engine
#   sweeper.RotationRate = "fast"             # wrong type
#   sweeper.Body.NoSuchThing                  # not a member of StaticMeshComponent
```

Uncomment one, run `pyright`, and read what comes back:

| Uncommented | Pyright |
|---|---|
| `sweeper.RotatoinRate = 45.0` | `Cannot assign to attribute "RotatoinRate" for class "BP_StreetSweeper_C"` |
| `sweeper.Body.RelativeLocation = target` | `Cannot assign to attribute "RelativeLocation" for class "StaticMeshComponent" — "UnrealVector" is not assignable to "property"` |
| `sweeper.RotationRate = "fast"` | `Cannot assign to attribute "RotationRate" for class "BP_StreetSweeper_C"` |
| `sweeper.Body.NoSuchThing` | `Cannot access attribute "NoSuchThing" for class "StaticMeshComponent"` |

Each of those is a thing that would otherwise have been a `NOT_FOUND` from the server, at whatever
point in the run reached that line. The read-only one is the interesting case: it never failed
loudly at all — a write to a `BlueprintReadOnly` property was simply refused.

## 9. Where the names come from

The import path mirrors the content path. `/Game/StreetSweeper/Blueprint/BP_StreetSweeper` is
`tempo_sample.reflected.Game.StreetSweeper.Blueprint.BP_StreetSweeper_C`; an engine class like
`/Script/Engine.Pawn` is `tempo_sample.reflected.Engine.Pawn`. Member names are Unreal's own,
verbatim, which is the point — a rename in C++ or in a Blueprint has to show up here as a different
name, or none of this would catch anything.

In your own project the distribution is `<your-project>` and the import is
`<your_project>.reflected`; everything after it is identical. The package exists even if your
project defines no services of its own: it then holds only the classes. That is also why the example
ships with TempoSample rather than with Tempo: the plugin's own [example
clients](../clients/examples.md) address everything by string and so run against any sim, while a
typed one names classes that exist in exactly one project. Copy it into yours and change the
imports.

## 10. Keep the classes and the sim together

Classes generated from one build and used against another is silent wrongness — the checker is
satisfied, and the first disagreement surfaces as something that looks like anything else. One call
settles it:

```python
world.check_schema()     # raises SchemaMismatch if they disagree
```

The example takes `--check-schema` to run it. It is explicit rather than automatic because the sim
answers by walking its whole reflection, which takes most of a minute — so it belongs once at the
start of a long run, or in a test.

## 11. Rust instead of Python

The same schema generates a Rust crate, with the same guarantee — and in Rust it is the compiler
rather than a separate checker, so there is nothing to configure and no way to skip it.

It is behind the same opt-in as the Rust RPC client, because a project that does not use Rust
should not pay for generating 16 MB of it:

```bash
TEMPO_GEN_RUST_API=1 Scripts/Package.sh
```

That leaves two crates beside each other:

```
Packaged/API/Rust/tempo-sim/          the client, the typed runtime and the published types
Packaged/API/Rust/tempo-sample/       this build's classes (module `reflected`) and its services
```

Point a scenario crate at both. The project crate depends on `tempo-sim` by relative path, so keep
them together or copy both:

```toml
[dependencies]
tempo-sim = { path = "../Packaged/API/Rust/tempo-sim" }
tempo-sample = { path = "../Packaged/API/Rust/tempo-sample" }
```

The classes are behind the crate's `reflected` feature, which is on by default; a crate that only
calls the project's services can turn it off with `default-features = false` and skip compiling
them. The feature also turns on tempo-sim's own `reflected`, which the project's re-exports come
from.

Then the same scenario as [step 6](#6-run-the-example), in Rust:

```rust
use tempo_sample::reflected::classes::game::street_sweeper::blueprint::BP_StreetSweeper_C;
use tempo_sim::{world, TempoError};

fn main() -> Result<(), TempoError> {
    tempo_sim::set_server("localhost", 10001);

    // The classes and the sim have to describe the same build.
    world::check_schema()?;

    for sweeper in world::get_actors::<BP_StreetSweeper_C>()? {
        // A component, typed as its own class.
        let lidar = sweeper.tempo_lidar();
        println!("{}x{} beams", lidar.horizontal_beams()?, lidar.vertical_beams()?);

        // Four writes, one round trip. Nothing is sent if the body fails.
        world::batch(|| {
            lidar.set_horizontal_beams(1024)?;
            lidar.set_vertical_beams(64)?;
            lidar.set_horizontal_fov(90.0)?;
            lidar.set_max_distance(20000.0)        // centimetres -- 200 m
        })?;

        // RelativeLocation is declared five classes up, on SceneComponent.
        println!("{:?}", lidar.relative_location()?);
    }
    Ok(())
}
```

`cargo build` is the whole check. The four cases from [step 8](#8-watch-a-rename-get-caught) fail
like this:

```text
error[E0599]: no method named `horizontal_beems` found for struct `TempoLidar`
error[E0599]: no method named `set_b_color_enabled` found for struct `TempoLidar`
error[E0308]: mismatched types: expected `i32`, found `&str`
error[E0599]: no method named `no_such_thing` found for struct `StaticMeshComponent`
```

Autocomplete comes from rust-analyzer with no configuration — it reads the crate you depend on.

Three things read differently from Python, and the
[reference](../clients/typed-classes.md#what-differs-from-python) has the full table:

- **Names are snake_case.** `HorizontalBeams` becomes `horizontal_beams`, and the rule handles
  acronyms — `FOVAngle` is `fov_angle`, not `f_o_v_angle`. A reserved word takes a trailing
  underscore, so a Blueprint component named `Box` is `sweeper.box_()`.
- **A read is a call that can fail**, not an attribute, so chaining through a reference unwraps
  first: `widget.target()?.visible()?`.
- **Async is a separate `_async` twin** — `lidar.horizontal_beams_async().await?` — matching every
  other generated Tempo Rust API.

## Next

- [Typed classes](../clients/typed-classes.md) — the reference: units and conversion, what is
  deliberately absent, how batching and snapshots behave, how to hold a scenario repository to the
  guarantee, and [when to write an RPC instead](../clients/typed-classes.md#when-to-write-an-rpc-instead).
- [TempoWorld](../plugins/tempo-world.md) — the string-keyed property API underneath, which stays
  unfiltered on purpose.
