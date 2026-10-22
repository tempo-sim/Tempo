# Typed classes, step by step

A scenario written against the string-keyed property API fails at the line that runs, in the middle
of a simulation, if a property was renamed since it was written. The
[typed classes](../clients/typed-classes.md) move that failure to your type checker. This walks from
a fresh checkout to a scenario a checker reads, and to watching it catch a rename.

What it assumes: a packaged build, Python 3.9 or newer, and nothing else.

## 1. Package the build

The classes are generated from the build they describe — walked out of that build's own Unreal
reflection — so they ship with it rather than from a release index:

```bash
./Plugins/Tempo/Scripts/Package.sh
ls Packaged/API/Python
```

```text
tempo-sample  tempo-sample-classes  tempo-sim
```

Three distributions, and you want all three:

| Distribution | What it is |
|---|---|
| `tempo-sim` | The client: the generated gRPC wrappers, the runtime behind attribute access, and `tempo_sim.world`. |
| `tempo-sample` | The host project's own services, if it defines any. Named after the project. |
| `tempo-sample-classes` | The typed classes for this build. Named after the project, so yours is `<your-project>-classes`. |

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
            Packaged/API/Python/tempo-sample/*.whl \
            Packaged/API/Python/tempo-sample-classes/*.whl
```

Importing the classes package is what registers it with the runtime, so the check is an import:

```bash
python -c "import tempo_sample_classes; from tempo_sim import world; print('ready')"
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

Then, from the project root:

```bash
python Plugins/Tempo/ExampleClients/Python/TypedClasses.py
```

```text
--- Spawning ---
spawned BP_StreetSweeper_C('BP_StreetSweeper_C_2147482057')

--- Properties ---
RotationRate was 60.0
RotationRate is now 120.0

--- Components ---
Body is a StaticMeshComponent
Body.RelativeLocation = Vector(X=0.0, Y=0.0, Z=500.0)
steering factor 1.0

--- Functions ---
moved to Vector(X=300.0, Y=0.0, Z=500.0)

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
  Tempo API that does not convert, and `tempo_sim.unreal.Vector` is not the `Vector` the other RPCs
  use. See [Units](../clients/typed-classes.md#units).
- **`is_a` asks about the actor**, not about the handle, which is what makes narrowing a broad query
  work.

## 7. Watch a rename get caught

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
| `sweeper.Body.RelativeLocation = target` | `Cannot assign to attribute "RelativeLocation" for class "StaticMeshComponent" — "Vector" is not assignable to "property"` |
| `sweeper.RotationRate = "fast"` | `Cannot assign to attribute "RotationRate" for class "BP_StreetSweeper_C"` |
| `sweeper.Body.NoSuchThing` | `Cannot access attribute "NoSuchThing" for class "StaticMeshComponent"` |

Each of those is a thing that would otherwise have been a `NOT_FOUND` from the server, at whatever
point in the run reached that line. The read-only one is the interesting case: it never failed
loudly at all — a write to a `BlueprintReadOnly` property was simply refused.

## 8. Where the names come from

The import path mirrors the content path. `/Game/StreetSweeper/Blueprint/BP_StreetSweeper` is
`tempo_sample_classes.Game.StreetSweeper.Blueprint.BP_StreetSweeper_C`; an engine class like
`/Script/Engine.Pawn` is `tempo_sample_classes.Engine.Pawn`. Member names are Unreal's own, verbatim,
which is the point — a rename in C++ or in a Blueprint has to show up here as a different name, or
none of this would catch anything.

In your own project the distribution is `<your-project>-classes` and the import is
`<your_project>_classes`; everything after the import is identical.

## 9. Keep the classes and the sim together

Classes generated from one build and used against another is silent wrongness — the checker is
satisfied, and the first disagreement surfaces as something that looks like anything else. One call
settles it:

```python
world.check_schema()     # raises SchemaMismatch if they disagree
```

The example takes `--check-schema` to run it. It is explicit rather than automatic because the sim
answers by walking its whole reflection, which takes most of a minute — so it belongs once at the
start of a long run, or in a test.

## Next

- [Typed classes](../clients/typed-classes.md) — the reference: what is deliberately absent, how
  batching and snapshots behave, and how to hold a scenario repository to the guarantee.
- [TempoWorld](../plugins/tempo-world.md) — the string-keyed property API underneath, which stays
  unfiltered on purpose.
