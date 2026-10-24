# Typed classes

The property API addresses everything by string:

```python
tempo_world.set_float_property(actor="Cat745_2", property="MaxSpeed", value=1200.0)
```

Nothing checks that spelling until the call runs. Rename `MaxSpeed` in C++ or in a Blueprint and
nothing breaks until a scenario reaches that line, mid-simulation, and gets `NOT_FOUND`.

The **typed classes** are the same capability with the strings removed:

```python
from tempo_sim import world
from tempo_sample.reflected.Game.Vehicles import BP_Cat745_C

for truck in world.get_actors(BP_Cat745_C):
    truck.MaxSpeed = 1200.0
```

Every name there is a declared member of a generated type. A misspelling, a property that was
removed, one whose type changed, one that became read-only — each is an error from your type checker
before anything runs.

!!! info "This is for catching drift, not for saving keystrokes"

    The reason to use it is that **a new release of the simulation makes scenarios that used removed
    or changed API fail under Pyright**, instead of failing at runtime days later. Completion and
    hover documentation come along for free, but they are not the point.

!!! tip "Setting this up for the first time"

    [Typed classes, step by step](../guides/typed-classes.md) walks from packaging to a type checker
    catching a rename, with a runnable example. This page is the reference behind it.

## Installing

The classes are generated from the build they describe, so they ship with it:

```bash
pip install Packaged/API/Python/tempo-sim/*.whl \
            Packaged/API/Python/tempo-sample/*.whl
```

They are the `reflected` subpackage of the project's own package, beside its services:
`tempo_sample.reflected` for TempoSample, `<your_project>.reflected` for yours, there even when the
project defines no services. Importing it is what registers the classes with the runtime.

### The published types

`tempo-sim` carries types of its own, for the modules every build has — the engine's always-loaded
ones and Tempo's — so code that touches only those needs no project package at all:

```python
from tempo_sim.reflected.Engine import Pawn
from tempo_sim.reflected.TempoSensors import TempoLidar
```

A project's package re-exports every one of these it describes alike, rather than generating a
second type for the same Unreal class: `tempo_sample.reflected.Engine.Pawn` *is*
`tempo_sim.reflected.Engine.Pawn`. So a helper library written against the published types takes a
project's Blueprints too. Where the two would differ — a class whose ancestry the published set
cannot carry, or one mentioning a type it does not have — the project defines its own.

!!! warning "Latest engine only"

    The published types are generated from the newest engine version Tempo supports. Against an
    older engine, code type-checks against the newest one's classes: a member or enumerator added
    since then passes the checker and fails at its first use, with the server naming what it does
    not have. `world.check_schema()` reports exactly which classes differ. A project's own types
    are generated from its real build, so they are exact for it.

## Finding things

Prefer discovery **by type**. There is no unchecked string in it at all:

```python
trucks = world.get_actors(BP_Cat745_C)      # list[BP_Cat745_C], including subclasses
```

By name also works, and is checked against the actor's real class:

```python
truck = world.get_actor("Cat745_2", BP_Cat745_C)   # raises if it is not one
```

Without a class you get a `DynamicObject`, where every attribute is permitted and checked only by
the server. It is a separate type so that giving up the guarantee is visible at the call site.

!!! warning "A name is not a stable address"

    An actor's name in a packaged build is not the label you saw in the editor
    ([Naming](../concepts/naming.md)), so a hard-coded name is its own kind of drift. Discovery by
    type avoids the question.

### Subsystems

A subsystem — world, game instance, engine or local player — is found by its class alone, since
there is one of each:

```python
from tempo_sample.reflected.TempoAgents import TempoAgentsWorldSubsystem

agents = world.get_subsystem(TempoAgentsWorldSubsystem)
agents.BrightnessMeterLateralOffset = 450.0
```

The handle is like any other: properties, functions, snapshots and batches all work on it. The
lookup asks the sim to resolve the subsystem, so one that declined to exist
(`ShouldCreateSubsystem`), or a class that is not a subsystem at all, fails there rather than at
first use. A local player subsystem is the first local player's.

## Reading and writing

Attribute access is the API. Reading asks the server; assigning tells it.

```python
truck.bEnabled = True
print(truck.Mass)
truck.PostProcessSettings.BloomIntensity = 2.0   # a struct, a member at a time
truck.WaypointTags = ["alpha", "bravo"]
```

A read-only property (`BlueprintReadOnly`) is generated as a property with no setter, so assigning
to it is a type error rather than a runtime surprise.

### Structs

A struct property reads as a live handle, so assigning to one of its members writes to the sim.
Most structs can also travel whole — every one whose members can each be carried as a value, which
is 1,230 of TempoSample's 1,558 — and those have a generated value type, built with keyword
arguments:

```python
from tempo_sample.reflected.structs import FWalkableSlopeOverride

mesh.SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlopeAngle=12.0))
current = mesh.GetWalkableSlopeOverride()                    # FWalkableSlopeOverride, a value
truck.Settings = FWheelSettings(Radius=40.0)                  # one write
copy = unreal.value_of(truck.Settings)                        # the whole struct, read once
```

**A struct write is a merge.** Only the members a value was given are sent, and the destination
keeps the rest — its current value for a property, its default for a function argument. So a value
with one member is the same write as assigning that member through the handle, and nothing you did
not mention is reset.

The constructor takes the members a scenario may write — the Blueprint-writable ones — while a value
read back from the sim carries all of them. `FHitResult` is the common case of a struct that is
read and never built: Blueprint itself reaches its members through a Break node.

A struct with a member that cannot travel — a map, a fixed-size array — has no constructor and
cannot be passed whole, but is still reachable a member at a time.

### Results by name

A method whose function also has out parameters returns every result keyed by name, because a
Blueprint usually hands back several things and position would be guesswork. The keys and their
types come from the signature:

```python
results = truck.K2_SetActorLocation(target, True, False)
moved: bool = results["ReturnValue"]
hit = results["SweepHitResult"]          # FHitResult
```

!!! warning "A write sets the property; it does not run what derives from it"

    Assigning to a property writes that property and nothing else. Unreal often recomputes something
    from it elsewhere — a component's world transform is recalculated in `UpdateComponentToWorld`,
    which a property write does not call — so `component.RelativeLocation = v` leaves the component
    with a new relative location and an unchanged position in the world.

    To *move* something, call a function: `SetActorTransform` and friends, or the component's own
    setter. The same applies to any property Unreal exposes through a `BlueprintSetter`, and those
    the typed API already routes through the accessor for you.

### Units

**The typed classes are the one corner of the Tempo API that speaks Unreal's own units**:
centimetres, degrees, left-handed. Raw `UPROPERTY` values carry no semantic tagging, so converting
them would be guesswork, and the typed API therefore does not convert at all.

!!! danger "Two `Vector` types"

    `tempo_sim.unreal.Vector` is centimetres and left-handed. `TempoCore.Geometry_pb2.Vector`, which
    every other RPC uses, is metres and right-handed. They are not interchangeable — convert
    explicitly.

### One call instead of twenty

Each attribute access is a round trip, which is the price of looking like a field. When that matters:

```python
with world.batch() as b:       # one RunBatch, in order, between two ticks of the sim
    truck.MaxSpeed = 1200.0
    truck.ScaleFactor = 2.0                     # a BlueprintSetter write: a call, same batch
    speed = b.get(lambda: truck.MaxSpeed)       # Deferred[float]
    moved = b.call(lambda: truck.TeleportTo(p, r, False, False))
print(speed.value, moved.value)

with truck.snapshot():         # one read; attribute reads served from it
    print(truck.MaxSpeed, truck.Mass, truck.Drag)
```

The server runs a whole batch in one go, so nothing simulates against a half-configured actor. A
read or call inside it comes back one of two ways:

- **Deferred**, through `b.get` / `b.call`: queued with everything else, its value available once
  the block has run. The lambda's own type is the `Deferred`'s, so a renamed property is still a
  type error inside it. It must make exactly one read or call and return it as it is.
- **Immediately**, by just reading or calling: what is queued so far goes out with it, and the real
  value comes back — so it sees the writes before it. A second round trip, and the batch has then
  partly run.

If an op fails, the server stops there and the block raises `BatchError` naming it. Nothing is sent
if the block raises before anything forced a flush.

A snapshot is a point-in-time copy: the simulation moves on and the values do not. A write inside
the block drops that entry, so a read after it sees the new value.

## Assets

Every asset the build cooked is in a generated catalog, one module per mount point, with folders as
namespaces. An asset-valued property is typed by the class it accepts:

```python
from tempo_sample.reflected.assets import Engine, Game

mesh.SetStaticMesh(Engine.BasicShapes.Cube)          # AssetRef[StaticMesh]
mesh.SetStaticMesh(Game.Materials.M_Paint)           # type error: AssetRef[Material]
mesh.SetStaticMesh(Game.Vehicles.SM_Truk)            # type error: no such asset
```

That is the point of it: a scenario naming an asset that was moved, renamed or dropped from the cook
stops type-checking, instead of failing with `NOT_FOUND` halfway through a run. `AssetRef` is
covariant, so a `Material` goes wherever a `MaterialInterface` is wanted.

The catalog is what the **packaged** build cooked, not what the editor has on disk — an asset that
nothing references is not cooked, and an entry for it would type-check and then fail. So it exists
only when the classes are generated against a packaged build, which `Package.sh` does. It leaves out
redirectors (what a move leaves at the old path — listing one would keep the old name compiling),
Blueprints (they are classes, and are already there as classes), and World Partition's generated
packages.

Names are the asset's own, mangled only where Python cannot spell them (`9Grid` is `_9Grid`). A map
usually sits beside a folder of the same name; the map keeps the name and the folder takes a
trailing underscore (`Maps.OpenWorld` and `Maps.OpenWorld_`). Two assets whose names still collide
are both left out, and generation says so.

A path that comes from data — a config file, a command line — can still be used, as
`AssetRef("/Game/…")`. It is untyped, deliberately and visibly, because nothing could check it.

`world.check_schema()` also compares the catalog against the sim's own asset registry, and names any
asset the package has and the sim does not.

## Spawning

```python
truck = world.spawn_actor(BP_Cat745_C, transform=t)       # -> BP_Cat745_C

with world.spawn_actor_deferred(BP_Cat745_C, transform=t) as truck:
    truck.InitialTerrainHeight = 1.0      # before the construction script and BeginPlay run
```

The deferred form is the only way to set a property that something reads during construction — a
plain spawn has already run both by the time you have a handle.

## Checking that the classes match the sim

Classes generated from one build and used against another is silent wrongness: the type checker is
satisfied and the first disagreement fails for reasons that look like anything else.

```python
world.check_schema()     # raises SchemaMismatch if they disagree
```

It is explicit rather than automatic because the sim answers by walking its whole reflection, which
takes most of a minute. Call it once from a test, or at the start of a long run.

The comparison is per class, so the report names what moved rather than only saying that something
did, and it distinguishes a class whose members changed from a class the sim does not have at all.
A class the *sim* has and your package does not is not a finding — that is the normal state of a
package describing part of a build.

## What is not there

By design, and in every case because the alternative is worse:

- **Types the API cannot carry** are absent rather than typed `Any`. A map as a whole value, a
  struct with a member that cannot travel, a C-style fixed array. `Any` would type-check and then
  fail.
- **Names Python cannot spell** — a Blueprint variable with a space in it. Reach those through the
  string-keyed API.
- **Functions the server will not invoke**: latent functions, and any whose parameters cannot be
  expressed as one value each.
- **Functions whose result cannot come back.** A method that answered `None` where its stub promised
  a value would be worse than none at all.
- **Editor-only classes, and editor-only members of the classes that remain.** The schema is
  generated in the editor, because only an editor can report a `BlueprintSetter` — and is then
  narrowed to what the packaged build reports from its own reflection. Both halves matter: `AActor`
  declares seven `WITH_EDITORONLY_DATA` properties and eleven editor-only functions that no
  packaged build has, and it is the root of two thousand classes.
- **Properties a class does not declare as Blueprint-accessible.** `BlueprintReadWrite` and
  `BlueprintReadOnly` are how a class says which of its properties are public API, as against the
  ones that are `UPROPERTY` only so the garbage collector or the serializer can see them. A
  `UPROPERTY(Transient)` holding a material instance is not something to offer a scenario.

    The server will still read and write them by string, since nothing about the transport cares. So
    reaching one through the string API is possible and is the escape hatch — it simply comes with
    no guarantee that it will still be there next release, which is the trade this page is about.

The string-keyed [property API](../plugins/tempo-world.md) remains for all of it, and stays
unfiltered on purpose.

## The same thing in Rust

The classes are generated for Rust too, from the same schema and with the same guarantee, as the
`reflected` module of the project's own crate — behind its `reflected` feature, which is on by
default. The crate ships beside `tempo-sim` when the build is packaged with `TEMPO_GEN_RUST_API=1`.
The published types are `tempo_sim::reflected`, behind tempo-sim's own `reflected` feature, which is
off by default and which a project's crate turns on for the ones it re-exports:

```toml
[dependencies]
tempo-sim = { path = "Packaged/API/Rust/tempo-sim" }
tempo-sample = { path = "Packaged/API/Rust/tempo-sample" }
```

```rust
use tempo_sample::reflected::classes::game::vehicles::BP_Cat745_C;
use tempo_sim::world;

for truck in world::get_actors::<BP_Cat745_C>()? {
    truck.set_max_speed(1200.0)?;
}
```

Rust states the guarantee more sharply than Pyright does in one respect: a read-only property has
no setter at all, so writing to one is `no method named set_max_speed`, which names the thing that
does not exist rather than complaining about an assignment.

### What differs from Python

| | Python | Rust |
|---|---|---|
| Member names | Unreal's own, verbatim — `truck.MaxSpeed` | snake_case — `truck.max_speed()` |
| Reading | an attribute — `truck.MaxSpeed` | a call that can fail — `truck.max_speed()?` |
| Writing | an assignment — `truck.MaxSpeed = 1200.0` | `truck.set_max_speed(1200.0)?` |
| Async | the same name, awaited | a separate `_async` twin |
| Inheritance | real in the stubs, flat at run time | a `Deref` chain, both |
| Batching | `with world.batch() as b:` | `world::batch(\|\| { ... })?` |
| Deferred read | `b.get(lambda: truck.MaxSpeed)` | `world::defer(\|\| truck.max_speed())?` |
| Streaming | `truck.watch("MaxSpeed")` — a string | `truck.watch_max_speed()?` — checked |
| Escape hatch | `DynamicObject`, a separate type | `truck.0`, the handle underneath |
| Subsystems | `world.get_subsystem(Cls)` | `world::get_subsystem::<Cls>()?` |

The names are the one deliberate divergence. Python keeps Unreal's spelling so that one name works
in the editor, the schema and the scenario; Rust uses the names a Rust programmer would guess. Both
are derived from the same schema, so a rename still has to show up in both.

Two consequences of reading being a call rather than an attribute:

```rust
// Python chains, because attribute access is implicit I/O:    w.Target.Visible
// Rust unwraps first, because the read is a call that can fail:
let visible = widget.target()?.visible()?;
```

```rust
// Several writes and a read, one round trip. Nothing is sent if the body returns an error first.
let mut beams = None;
world::batch(|| {
    lidar.set_horizontal_beams(1024)?;
    lidar.set_vertical_fov(30.0)?;
    beams = Some(world::defer(|| lidar.horizontal_beams())?);   // Deferred<i32>
    Ok(())
})?;
let beams = beams.unwrap().take()?;
```

Do nothing to the value inside `defer`'s closure: there is no value yet, and the read leaves the
closure through its `?`. `defer(|| Ok(lidar.horizontal_beams()? * 2))` defers the plain count, and
the doubling never happens — a change of *type* is refused, but one that keeps it cannot be told
apart from the read. Python cannot make that mistake, because the placeholder will not multiply. A
method with out parameters cannot be deferred at all; call it directly.

### Assets, in Rust

The catalog is a module tree of statics, `assets::engine::basic_shapes::Cube`, each an
`AssetRef<StaticMesh>`. An `AssetRef` derefs along the same chain the class handles do, so an
asset-valued setter takes a reference and a derived class coerces into it:

```rust
use tempo_sample::reflected::assets::engine::basic_shapes::Cube;
use tempo_sample::reflected::assets::game::materials::M_Paint;

mesh.set_static_mesh(&Cube)?;                        // &AssetRef<StaticMesh>
mesh.set_material(0, &M_Paint)?;                     // &AssetRef<Material> -> &AssetRef<MaterialInterface>
mesh.set_static_mesh(&AssetRef::from_path(path))?;   // a path from data: untyped, visibly
```

Rust keeps modules and statics apart, so a map and the folder beside it keep the same name there.

### What is not there, in Rust specifically

Everything in [What is not there](#what-is-not-there) applies, plus two Rust-only omissions:

- **Container arguments to functions.** A `TArray` parameter of enums or object references would
  need a slice of a generated type, which Rust's orphan rule keeps out of the generic conversion.
  Container *properties* of those kinds are fine; it is only function parameters.
- **Struct value fields holding a container of enums or of handles.** A struct value is otherwise
  complete; such a field is left out, and since a write is a merge, leaving it out leaves the
  destination's own value in place.

An *out* parameter that cannot be carried is dropped from the tuple and the function stays: the
caller never supplies one, so it does not stop the call.

Struct values are plain Rust structs with every field an `Option`, because an unset field is not
sent. A struct property's accessor returns its proxy, which loads and stores the whole value:

```rust
use tempo_sample::reflected::structs::FWalkableSlopeOverride;

mesh.set_walkable_slope_override(FWalkableSlopeOverride {
    walkable_slope_angle: Some(12.0),
    ..Default::default()
})?;
let (moved, hit) = truck.k2_set_actor_location(target, true, false)?;   // hit: FHitResult
let copy = truck.settings().load()?;                                    // the whole struct
truck.settings().radius()?;                                             // or one member
```

Conversely, one Python concession is unnecessary here. The Python stubs must deliberately *omit*
`__getattr__`, because a catch-all a type checker can see makes every attribute legal on every
generated class. Rust has no catch-all to hide: an undeclared method is an error with nothing that
could suppress it. That is also why a root class is generated without a `Deref` — it keeps the
handle's own string-keyed methods behind an explicit `.0` rather than offering them on everything.

Functions that return more than one value hand back a tuple — the return value first, then the out
parameters in declaration order — but each element is read by the name it was declared with, so
adding a parameter to a Blueprint cannot silently shift the others onto the wrong values.

## Keeping a scenario repository honest

The generated stubs make drift *detectable*; your CI is what makes it *detected*. Pyright enforces
nothing unless it is required to run. At minimum:

```json
{
  "typeCheckingMode": "standard",
  "reportUnknownMemberType": "error",
  "reportUnknownArgumentType": "error",
  "reportUnnecessaryTypeIgnoreComment": "error"
}
```

That last one is what stops a `# type: ignore` outliving the thing it silenced.

For the release side, `diff_schema.py` compares two builds' schemas and lists what a client could
break against, exiting non-zero on anything breaking — so a breaking change is a decision made
before release rather than a discovery after it.
