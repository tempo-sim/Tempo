# Reflected API

`TempoWorld`'s property API addresses everything by string:

```python
from tempo_sim.TempoWorld.WorldControl_pb2 import Value

tempo_world.set_property(actor="Truck_2", property="MaxSpeed", value=Value(float_value=1200.0))
```

Nothing checks that spelling until the call runs. Rename `MaxSpeed` in C++ or in a Blueprint and
nothing breaks until a scenario reaches that line, mid-simulation, and gets `NOT_FOUND`.

The **reflected API** is the same capability with the strings removed, generated from Unreal's own
reflection on your project:

```python
from tempo_sim import world
from tempo_sample.reflected.Game.Vehicles import BP_Truck_C

for truck in world.get_actors(BP_Truck_C):
    truck.MaxSpeed = 1200.0
```

Every name there is a declared member of a generated type. A misspelling, a property that was
removed, one whose type changed, one that became read-only — each is an error from your type checker
before anything runs.

!!! tip "Setting this up for the first time"

    [The reflected API, step by step](../guides/reflected-api.md) walks from packaging to a type checker
    catching a rename, with a runnable example. This page is the reference behind it.

## Before you start

Five things about the reflected API to note..

1. **It speaks Unreal's units and conventions, not Tempo's.** Centimeters, degrees and a left-handed frame, like
   the TempoWorld property and function RPCs it is built on and unlike the rest of Tempo's API. Its value types are Unreal's — `FVector`, `FRotator`, `FTransform` — shaped as Unreal's are, and `tempo_sim.conversion` converts to and from SI. See [Units](#units).
2. **It comes from your packaged build.** The classes are generated per project, from the build
   they describe — its exact engine version, plugins, C++ and Blueprints — so they exist only once
   you have packaged it. `pip install tempo-sim` alone gives you none. See [Installing](#installing).
3. **A class offers only what it exposes to Blueprint.** A property needs `BlueprintReadWrite` or
   `BlueprintReadOnly`, and a function needs `BlueprintCallable` or `BlueprintPure`. Without one of
   those it is absent. See
   [What a class offers](#what-a-class-offers).
4. **Two packages carry it.** `tempo-sim` has the runtime, and your project's own package its classes,
including its Blueprints and its asset catalog. See [What comes from where](#what-comes-from-where).
7. **It doesn't replace all RPCs.** Calling a function through it is a quick way to reach
   something, not a way to design an API. Something that streams, or that finishes later than the
   call, wants an RPC. See [When to write an RPC instead](#when-to-write-an-rpc-instead).

## Installing

The classes are generated from the build they describe, when it is packaged (`Package.sh` runs
`Scripts/GenReflectedAPI.sh`), and they ship with it, in the project's own package:

```bash
pip install Packaged/API/Python/tempo-sample/*.whl
```

The project's wheel pins the `tempo-sim` version it was generated against, so pip installs that
from PyPI beside it. Packaging also installs the classes into the project's `TempoEnv`, which
already has both packages.

They are the `reflected` subpackage of the project's own package, beside its services:
`tempo_sample.reflected` for TempoSample, `<your_project>.reflected` for yours, there even when the
project defines no services. Importing it is what registers the classes with the runtime.

### What comes from where

| | `tempo-sim` (`tempo_sim`, `tempo_sim::`) | Your project's package (`tempo_sample`, `tempo_sample::`) |
|---|---|---|
| **Runtime** | `unreal` (the handle classes, Unreal's value types, `AssetRef`), `world` (discovery, spawning, components, transforms, world state, `batch`, `check_schema`; the mirror of `tempo_world`, in SI as `tempo_world` is), `conversion` | none; it uses tempo-sim's |
| **Classes** | none | `reflected`: every class in *this* build, including its C++ modules, its Blueprints, the plugins it enables and every engine module it loads |
| **Asset catalog** | none | `reflected.assets`, everything this build cooked |
| **Services** | Tempo's RPC wrappers (`tempo_world`, `tempo_sensors`, …) | the project's own, if it defines any |
| **Rust feature** | none | `reflected`, on by default |

### `unreal` and `world`

The runtime is two modules, split along the [units](#units) boundary. The same names appear in all
three clients: `tempo_sim.unreal` / `tempo_sim.world` in Python, `tempo_sim::unreal` /
`tempo_sim::world` in Rust, and `tempo::unreal` / `tempo::world` in C++.

**`unreal` is Unreal's own vocabulary**: the types that a reflected class is built from, or that it
hands back. They are named and shaped as Unreal's are, in Unreal's units, which is where the module
gets its name. It holds:

- **Unreal's value types**: `FVector`, `FRotator`, `FTransform`, `FColor` and the rest (see
  [Unreal's own types](#unreals-own-types)), in centimetres, degrees and a left-handed frame.
- **The handle base** that every generated class is built on: `Object` in Python, `ObjectRef` in
  Rust, `Handle` in C++. Python also has `DynamicObject`, the unchecked handle you get when you name
  no class.
- **`AssetRef`**, the type of every entry in the asset catalog, typed by the asset's class.
- **The plumbing behind the reflected API**: the class registry that importing your project's
  `reflected` package fills, and the deferred reads a batch hands back (`Deferred`).

Your code imports from `unreal` mainly to build a value to pass in, for example
`from tempo_sim.unreal import FVector`. The rest arrives through the generated classes.

**`world` is where a scenario starts**: finding actors (`get_actors`, `get_actor`,
`get_subsystem`), spawning them, `batch` and `check_schema`. Apart from those entry points, it
mirrors `tempo_world`: the same functions under the same names, taking handles where `tempo_world`
takes names. It also speaks SI as `tempo_world` does, so `world.spawn_actor` takes a
`world.Transform` in metres and radians.

So a value in centimetres comes from `unreal` and a value in metres comes from `world`, and moving
one into the other is always an explicit [conversion](#converting).

### Exact for your build

**The classes describe the build they were generated from, exactly**: its engine version down to
the hotfix, the plugins it enables, its own C++ modules and its Blueprints. Nothing in them is a
guess about some other build, so there is no older-engine caveat — a project on any engine Tempo
supports gets the classes of that engine.

The price is that they exist only once the project has been packaged, and are regenerated each time
it is. After changing a `UPROPERTY` or a Blueprint, package again (or run
`Scripts/GenReflectedAPI.sh` against the existing package) to see the change.

## Finding things

Prefer discovery **by type**:

```python
trucks = world.get_actors(BP_Truck_C)      # list[BP_Truck_C], including subclasses
```

By name also works, and is checked against the actor's real class:

```python
truck = world.get_actor("Truck_2", BP_Truck_C)   # raises if it is not one
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
    the reflected API already routes through the accessor for you.

### Units

**The reflected API speaks Unreal's own units**: centimetres, degrees, left-handed (X forward, Y
right, Z up), as the [property and function RPCs](../concepts/conventions.md#the-exception-set_property)
it is built on do. The rest of the Tempo API speaks metres, radians and a right-handed frame (X
forward, Y left, Z up). Raw `UPROPERTY` values carry no semantic tagging, so converting them would be
guesswork, and the reflected API therefore does not convert at all.

That covers everything that goes through a reflected class: property reads and writes, function
arguments and results, struct members, `watch`, batches and snapshots. A `MaxDistance` of `20000.0`
is 200 metres, and a `RelativeRotation` yaw of `90.0` is a quarter turn to the right. `world`, on
the other hand, mirrors `tempo_world` and speaks SI like it: `spawn_actor` takes a `Transform` in
metres and radians, as `tempo_world.spawn_actor` does.

#### Unreal's own types

The values the reflected API carries are Unreal's own, named and shaped as Unreal's are, in
`tempo_sim.unreal`, `tempo_sim::unreal` and `tempo::unreal`:

| Type | Shape | |
|---|---|---|
| `FVector`, `FVector2D` | `X`, `Y`(, `Z`) | centimetres |
| `FIntVector`, `FIntPoint` | `X`, `Y`(, `Z`) | |
| `FRotator` | `Pitch`, `Yaw`, `Roll` | degrees, constructed in that order, as Unreal's is |
| `FQuat` | `X`, `Y`, `Z`, `W` | |
| `FTransform` | `Rotation` (an `FQuat`), `Translation`, `Scale3D` | scale 1 by default |
| `FColor` | `R`, `G`, `B`, `A` | 8-bit sRGB, opaque by default |
| `FLinearColor` | `R`, `G`, `B`, `A` | floating-point, linear, opaque by default |

Member names follow each language's convention, as everywhere in the reflected API: `Pitch` in
Python, `pitch` in Rust and C++. So `FRotator(10, 20, 30)` means what it does in Unreal — pitch 10,
yaw 20, roll 30 — and an `FTransform` carries its scale, which a write keeps rather than resetting
to 1.

They are not the TempoCore messages every other RPC uses: `FVector` is centimetres and left-handed,
and `TempoCore.Geometry_pb2.Vector` (`tempo_sim::proto::tempo_core::Vector`) is metres and
right-handed. To move a value between the two, convert it explicitly.

#### Converting

`ue_to_si` and `si_to_ue` are overloaded on what they are given, and a type checker knows what each
returns:

=== "Python"

    ```python
    from tempo_sim import tempo_world, world
    from tempo_sim.conversion import si_to_ue, ue_to_si

    location = sweeper.K2_GetActorLocation()              # FVector, centimetres
    truck = world.spawn_actor(BP_Truck_C, transform=world.Transform(location=ue_to_si(location)))

    pose = si_to_ue(tempo_world.get_current_actor_state(actor="Marker").transform)   # FTransform
    reach = ue_to_si(sweeper.TempoLidar.MaxDistance)      # a float: centimetres to metres
    ```

=== "Rust"

    ```rust
    use tempo_sim::conversion::{si_to_ue, ue_to_si};

    let location = sweeper.k2_get_actor_location()?;     // FVector, centimetres
    let metres = ue_to_si(&location);                    // world::Vector, metres

    let state = tempo_sim::tempo_world::get_current_actor_state(tempo_sim::tempo_world::ActorStateRequest {
        actor: "Marker".into(),
        ..Default::default()
    })?;
    let pose = si_to_ue(&state.transform.unwrap_or_default());   // FTransform
    ```

=== "C++"

    ```cpp
    #include <tempo/conversion.h>

    auto location = sweeper.k2_get_actor_location();               // Result<FVector>, centimetres
    tempo::world::Vector metres = tempo::conversion::ue_to_si(location.value());

    auto state = tempo::tempo_world::get_current_actor_state({.actor = "Marker"});
    tempo::unreal::FTransform pose = tempo::conversion::si_to_ue(state.value().transform());
    ```

| Unreal | TempoCore | Rule |
|---|---|---|
| a number | a number | a length: cm ↔ m |
| `FVector` | `Vector` | a length: cm ↔ m, Y negated |
| `FVector2D` | `Vector2D` | likewise |
| `FRotator` | `Rotation` | degrees ↔ radians, pitch and yaw negated, roll kept |
| `FQuat` | `Quat` | exact: X and Z negated |
| `FTransform` | `Transform` | its translation and rotation, each as above, and its scale as given |

These are the server's own rules ([Conventions](../concepts/conventions.md)), and the rotation math
is Unreal's own: `rotator_of` and `quat_of` reproduce `FQuat::Rotator()` and
`FRotator::Quaternion()` formula for formula, singularities included. For an `FQuat` as Euler
angles, convert the rotator Unreal would make of it: `ue_to_si(rotator_of(q))`.

!!! warning "What does not survive a conversion"

    - **A direction is not a length.** A number or a vector is taken to be a length and scaled —
      right for a location or a velocity, wrong for a normal or a unit axis. For those use
      `ue_direction_to_si` and `si_direction_to_ue`, which change the handedness and nothing else.
      An angle on its own is not converted at all, since whether its sign changes depends on the
      axis it turns about; neither is an angular velocity.
    - **A transform's rotation, through Euler angles.** `Transform.rotation` is a `Rotation`, so an
      `FTransform`'s quaternion reaches it through `rotator_of`, singularities included. Convert
      `FTransform.Rotation` on its own for an exact `Quat`.
    - **Integer vectors and colors** have no TempoCore counterpart and are not converted.

### One call instead of twenty

Each attribute access is a round trip, which is the price of looking like a field. When that matters:

```python
with world.batch() as b:       # one RunBatch, in order, between two ticks of the sim
    truck.MaxSpeed = 1200.0
    truck.ScaleFactor = 2.0                     # a BlueprintSetter write: a call, same batch
    speed = b.get(lambda: truck.MaxSpeed)       # Deferred[float]
    moved = b.call(lambda: truck.K2_TeleportTo(p, r))
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

A batch opened inside another joins it, so the whole block is still one call, in order.

### From a coroutine

Attribute access is synchronous, because an assignment cannot be awaited. Made inside a coroutine,
a blocking round trip would wait on the very event loop its reply has to arrive on, so it fails
with an error naming the form to use. The async forms are these:

```python
async with world.batch() as b:     # sent, without blocking, when the block ends
    truck.MaxSpeed = 1200.0        # an assignment only queues, so it is fine here
    speed = b.get(lambda: truck.MaxSpeed)
print(speed.value)

speed = await world.get_async(lambda: truck.MaxSpeed)        # one read, typed as float
moved = await world.call_async(lambda: truck.K2_TeleportTo(p, r))

async with truck.snapshot_async():  # one read; attribute reads served from it
    print(truck.MaxSpeed, truck.Mass)

async for speed in truck.watch_async("MaxSpeed"):
    ...
```

Inside `async with world.batch()` nothing is sent until the block ends, so a read or call there
goes through `b.get` / `b.call`; one made directly would have to block, and is refused. A batch is
per task, not per thread: two coroutines on one loop each have their own, and one awaiting inside
its batch does not capture the other's writes.

`world`'s functions have async twins as well, named with `_async` as Rust's are:
`await world.spawn_actor_async(Cls)`, `await world.destroy_actor_async(truck)`,
`async with world.spawn_actor_deferred_async(Cls) as truck`, `async for state in
world.stream_actor_state_async(truck)`, and so on through the module. Two names rather than
`tempo_world`'s one name serving both, because a type checker reads that one as always returning a
coroutine.

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

## The world module

`world` is the reflected API's counterpart of `tempo_world`, and mirrors it: the same functions, the same
arguments in the same SI units — metres, radians, right-handed — with a handle wherever
`tempo_world` takes an actor's or a component's name, and a class wherever it takes a class's. Each
returns what its `tempo_world` namesake returns, except that what makes a thing hands back a typed
handle to it.

```python
t = world.Transform(location=world.Vector(z=5.0))        # SI, as tempo_world takes it: 5 m up
truck = world.spawn_actor(BP_Truck_C, transform=t)       # -> BP_Truck_C

with world.spawn_actor_deferred(BP_Truck_C, transform=t) as truck:
    truck.MaxSpeed = 1200.0      # before the construction script and BeginPlay run

light = world.add_component(PointLightComponent, truck, name="Beacon")   # -> PointLightComponent
world.set_actor_location(truck, world.Vector(x=10.0), relative_to_actor=marker)
pose = world.get_actor_transform(truck).transform          # a TempoCore Transform, in SI
world.destroy_component(light)
world.destroy_actor(truck)
```

The deferred form is the only way to set a property that something reads during construction — a
plain spawn has already run both by the time you have a handle.

| | `world` |
|---|---|
| Discovery | `get_actors(Cls)`, `get_actor(name, Cls)`, `get_components(actor, Cls)`, `get_subsystem(Cls)`; untyped, `get_actor(name)` (Python) or `find_actor(name)` (Rust, C++) |
| Actors and components | `spawn_actor`, `spawn_actor_deferred`, `destroy_actor`, `add_component`, `destroy_component`, `activate_component`, `deactivate_component` |
| Transforms | `set_`/`get_` `actor_`/`component_` `transform`, `location`, `rotation`, `scale3d` |
| World state | `get_current_actor_state`, `stream_actor_state`, `get_current_actor_states_near`, `stream_actor_states_near`, `stream_overlap_events`, `raycast` |
| Batching and checking | `batch`, `batch_async` (Rust), `defer` (Rust, C++), `check_schema`; in Python, `get_async` and `call_async` |

Every function in the first four rows has an `_async` twin in Python and Rust, taking the same
arguments; the last row lists the async forms themselves.

`world` also re-exports the TempoCore types it takes — `world.Vector`, `world.Rotation`,
`world.Transform`, `world.Vector2D` — so a scenario using it needs no proto import. A handle of the
wrong kind is refused before anything is sent: a component where an actor is wanted, or the other
way round. The units are the one thing to keep straight: `world` is SI like `tempo_world`, and the
generated classes are Unreal's own; `ue_to_si` and `si_to_ue` move between them.

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
A class the *sim* has and your package does not is not a finding: nothing written against the package
can address it, so nothing can fail because of it.

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
- **Properties and functions a class does not expose to Blueprint.** See
  [What a class offers](#what-a-class-offers).

The string-keyed [property API](../plugins/tempo-world.md) remains for all of it, and stays
unfiltered on purpose.

## What a class offers

**A member reaches the reflected API only if its class exposes it to Blueprint.** The specifier
decides, and nothing else does:

| Declared as | In the reflected API |
|---|---|
| `UPROPERTY(BlueprintReadWrite)` | read and write |
| `UPROPERTY(BlueprintReadOnly)` | read only: no setter in Python, and no `set_` method in Rust |
| `UPROPERTY(BlueprintReadWrite, BlueprintSetter=SetFoo)` | read and write, and a write calls `SetFoo` |
| `UPROPERTY()`, `EditAnywhere`, `VisibleAnywhere` or anything else without one of the two above | **absent** |
| `UFUNCTION(BlueprintCallable)` or `UFUNCTION(BlueprintPure)` | a method |
| `UFUNCTION()` without either, including a `BlueprintImplementableEvent` that is not also callable | **absent** |

`VisibleAnywhere` and `EditAnywhere` are about the editor's Details panel and say nothing to
Blueprint, so a property declared with only those is absent, not read-only. Variables and functions
authored in a Blueprint are Blueprint-visible already, so they appear unless the variable is marked
read-only, which makes it read-only here too.

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar")
int32 HorizontalBeams = 1024;       // read and write

UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lidar")
float LastMeasurementTime = 0.0f;   // read only

UPROPERTY(EditAnywhere, Category = "Lidar")
float InternalGain = 1.0f;          // absent: no Blueprint specifier

UPROPERTY(Transient)
UMaterialInstanceDynamic* Material; // absent

UFUNCTION(BlueprintCallable, Category = "Lidar")
void Recalibrate();                 // a method
```

This is deliberate, because the guarantee is only as good as the surface it covers. A class has
plenty of `UPROPERTY`s that exist so the garbage collector, the serializer or the editor can see
them, and offering those to scenarios would make every internal change a breaking one. The
Blueprint specifier is the declaration Unreal already has for "this is meant to be used from
outside", so the reflected API uses it as its own.

**If something you need is missing, the fix is in the class.** Add `BlueprintReadWrite`,
`BlueprintReadOnly` or `BlueprintCallable`, rebuild, and regenerate (`Package.sh` does that). For a
class you do not own, the string-keyed [property API](../plugins/tempo-world.md) reaches every
property regardless, but without the guarantee that it will still be there next release.

## When to write an RPC instead

The reflected API can call any Blueprint-callable function, so it is tempting to treat it as a
way to add API without writing any: put a `UFUNCTION(BlueprintCallable)` on a class and call it from
Python. For a one-off, or for a function that already exists, that is fine. For something clients
will depend on, a [proper RPC](../guides/custom-services.md) is usually better. The reasons, roughly
in order of how often they decide it:

- **It streams.** An RPC can be server-streaming: the server sends a message whenever it has one,
  and the client iterates over them. A function call returns one result. `watch` can stream one
  property, but only as that property's value sent once per tick, whether it changed or not. Events
  (a collision, a lane change, a traffic light turning), bulk data at its own rate (images, point
  clouds) and anything computed rather than stored need a streaming RPC, which is how Tempo's
  sensors work.
- **It finishes later than the call.** A reflected call runs the function on the game thread and
  answers in the same tick with whatever it returned. That is also why latent functions are left
  out: their result arrives later and the call cannot wait for it. An RPC handler can hold its
  response until the work is done, such as arriving at a destination, finishing a render readback,
  or stepping some number of frames, so the client makes one call and awaits it instead of polling.
- **It should speak SI.** An RPC converts its values once, in the server, which knows what each
  one means: a location is scaled, a direction is not, a scale is left alone. A reflected
  function hands every client raw Unreal values to convert itself.
- **It is a contract, not an implementation detail.** A `.proto` is an API you design and version
  on purpose. A function's signature changes whenever someone edits the C++ or the Blueprint. The
  reflected API makes that change a type error, but an RPC keeps it from happening at all. And to be
  reachable, the function has to be `BlueprintCallable`, which also exposes it to every Blueprint.
- **Other clients need it.** The C++ client and the ROS bridge are generated from the protos and
  know nothing about the reflected API.
- **It moves a lot of data.** Every reflected value travels as a self-describing `Value`, and every
  call names its target and member as strings for the server to look up through reflection. A
  purpose-built message, such as the packed arrays a lidar scan uses, is far cheaper for bulk data.
  A batch reduces the number of round trips, but not the cost of each value.
- **It needs a type the reflected API cannot carry**, such as a map, a fixed-size array, a delegate
  or a struct with one of those inside (see [What is not there](#what-is-not-there)).

The reflected API is the right tool for what it was built for: configuring and observing what
a class already exposes, and calling the functions it already has, with a type checker catching the
day any of it changes.

## The same thing in Rust

The classes are generated for Rust too, from the same schema and with the same guarantee, as the
`reflected` module of the project's own crate — behind its `reflected` feature, which is on by
default. The crate ships in `Packaged/API/Rust` when the build is packaged with
`TEMPO_GEN_RUST_API=1`, and depends on `tempo-sim` from crates.io, as yours does:

```toml
[dependencies]
tempo-sim = "0.4"
tempo-sample = { path = "Packaged/API/Rust/tempo-sample" }
```

```rust
use tempo_sample::reflected::classes::game::vehicles::BP_Truck_C;
use tempo_sim::world;

for truck in world::get_actors::<BP_Truck_C>()? {
    truck.set_max_speed(1200.0)?;
}
```

Rust states the guarantee more sharply than Pyright does in one respect: a read-only property has
no setter at all, so writing to one is `no method named set_max_speed`, which names the thing that
does not exist rather than complaining about an assignment.

Every member has an `_async` twin, struct members included —
`camera.post_process_settings().set_white_temp_async(6500.0).await?` — and a batch has one too.
Its closure is synchronous, since inside a batch a setter only queues and there is nothing to
wait for; the send is what is awaited:

```rust
let mut speed = None;
world::batch_async(|| {
    truck.set_max_speed(1200.0)?;
    speed = Some(world::defer(|| truck.max_speed())?);
    Ok(())
})
.await?;
let speed: f64 = speed.unwrap().take()?;
```

A read made directly inside `batch_async`, rather than through `defer`, would have to block, and
is refused. The returned future holds only what was queued, so it can be awaited in a spawned task.
A blocking method called from async code panics inside the runtime, as every blocking Tempo Rust
function does, so async code uses the twins throughout.

The classes register themselves when the program starts, so `world::check_schema()` can be the
first thing it does. That needs the program to use the crate's classes somewhere: a dependency
that is never named is not linked at all.

### What differs from Python

| | Python | Rust |
|---|---|---|
| Member names | Unreal's own, verbatim — `truck.MaxSpeed` | snake_case — `truck.max_speed()` |
| Reading | an attribute — `truck.MaxSpeed` | a call that can fail — `truck.max_speed()?` |
| Writing | an assignment — `truck.MaxSpeed = 1200.0` | `truck.set_max_speed(1200.0)?` |
| Async | `async with world.batch()` to write, `await world.get_async(lambda: ...)` to read | a separate `_async` twin, and `world::batch_async` |
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

## The same thing in C++

The C++ client has the reflected API too, generated from the same schema with the same guarantee —
here the compiler's. It describes the project's whole build, as `tempo::reflected`, in a second
archive, `libtempo_reflected`, which `Package.sh` builds and ships in `Packaged/API/Cpp` when
`TEMPO_GEN_CPP_API=1`.

```cpp
#include <tempo/world.h>
#include <tempo/reflected/classes/game/vehicles/BP_Truck_C.h>

using tempo::reflected::classes::game::vehicles::BP_Truck_C;

auto trucks = tempo::world::get_actors<BP_Truck_C>();
if (!trucks) return 1;
for (const BP_Truck_C& truck : trucks.value()) truck.set_max_speed(1200.0);
```

It mirrors the Rust surface, with C++'s own means where they serve better:

| | Rust | C++ |
|---|---|---|
| Names | snake_case | the same names |
| Errors | `Result<T, TempoError>` and `?` | `tempo::Result<T>`; nothing throws |
| Inheritance | a `Deref` chain | C++ inheritance: `class Pawn : public Actor` |
| A class, as a bound | `T: Class` | the `tempo::unreal::Class` concept |
| Out parameters | a tuple | a `std::tuple`, unpacked with a structured binding |
| Struct values | `Option` fields | `std::optional` fields |
| Async | `_async` twins | none: the C++ client is synchronous |
| Batching | `world::batch(\|\| { ... })` | `world::batch([&]() -> Result<void> { ... })` |

A class's header includes its superclass's and forward-declares the rest, so a scenario includes
the headers of the classes it uses; definitions are compiled into the archive, so including a
module costs only its declarations. See [C++ client](cpp.md) for linking it.

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
