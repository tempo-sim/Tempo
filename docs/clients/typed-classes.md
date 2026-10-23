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
from tempo_sample_classes.Game.Vehicles import BP_Cat745_C

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
            Packaged/API/Python/tempo-sample-classes/*.whl
```

The distribution is named after the project — `tempo-sample-classes` for TempoSample,
`<your-project>-classes` for yours — and importing it is what registers it with the runtime.

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
with world.batch():            # writes buffered, sent together
    truck.MaxSpeed = 1200.0
    truck.Mass = 1500.0

with truck.snapshot():         # one read; attribute reads served from it
    print(truck.MaxSpeed, truck.Mass, truck.Drag)
```

A snapshot is a point-in-time copy: the simulation moves on and the values do not. A write inside
the block drops that entry, so a read after it sees the new value.

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

## What is not there

By design, and in every case because the alternative is worse:

- **Types the API cannot carry** are absent rather than typed `Any`. A map, an arbitrary struct as a
  whole value, a C-style fixed array. `Any` would type-check and then fail.
- **Names Python cannot spell** — a Blueprint variable with a space in it. Reach those through the
  string-keyed API.
- **Functions the server will not invoke**: latent functions, and any whose parameters cannot be
  expressed as one value each.
- **Editor-only classes.** The schema is generated in the editor but filtered to what the packaged
  build actually ships, so the classes you are offered are classes that exist.
- **Properties a class does not declare as Blueprint-accessible.** `BlueprintReadWrite` and
  `BlueprintReadOnly` are how a class says which of its properties are public API, as against the
  ones that are `UPROPERTY` only so the garbage collector or the serializer can see them. A
  `UPROPERTY(Transient)` holding a material instance is not something to offer a scenario.

    The server will still read and write them by string, since nothing about the transport cares. So
    reaching one through the string API is possible and is the escape hatch — it simply comes with
    no guarantee that it will still be there next release, which is the trade this page is about.

The string-keyed [property API](../plugins/tempo-world.md) remains for all of it, and stays
unfiltered on purpose.

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
