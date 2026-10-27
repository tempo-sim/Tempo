# TempoWorld

TempoWorld is how a client queries and controls the state of the simulated world: what exists,
where it is, what its properties are, and what it can be told to do.

It leans on Unreal's reflection system, which is why it needs no engine code from you — any C++
or Blueprint class, any `UPROPERTY`, any `UFUNCTION` is reachable over the wire.

!!! info "Read these first"

    TempoWorld converts between Unreal's centimeters/degrees/left-handed world and the API's
    meters/radians/right-handed one — with one deliberate exception. And it addresses things by
    name, which behaves differently in the Editor and in a packaged build.

    [Units and coordinates](../concepts/conventions.md) &nbsp;·&nbsp;
    [Naming](../concepts/naming.md)

## Querying world state

TempoWorld can **get** (right now) or **stream** (continuously) the state of actors. State
includes the name, transform, 6-DoF velocity (linear m/s, angular rad/s), 3D bounds, and the
timestamp when it was captured. The angular velocity is the right-handed angular velocity vector,
the same one [TempoMovement](tempo-movement.md#velocity-and-acceleration-commands) commands take;
see [Angular velocity](../concepts/conventions.md#angular-velocity).

```python
import tempo_sim.tempo_world as tw

# Right now
tw.get_current_actor_state(actor="MyActor")

# Continuously
for state in tw.stream_actor_state(actor="MyActor"):
    ...
```

Often you care not about every actor but about the ones *near* the one you are controlling or
collecting data from:

```python
# Everything within 50 m of MyActor (including MyActor itself)
tw.get_current_actor_states_near(near_actor="MyActor", search_radius_m=50.0)

for state in tw.stream_actor_states_near(near_actor="MyActor", search_radius_m=50.0):
    ...
```

You can also query around a **point** rather than an actor, with
`get_current_actor_states_near_position`.

Overlaps are available as a stream too:

```python
for overlap_event in tw.stream_overlap_events(actor="MyActor"):
    ...
```

Each event names the actor whose subscription fired, the actor that overlapped it, and that
actor's class.

For a pose on its own — and for anything below the actor, since actor state has no component
breakdown — reach for the transform getters in
[Reading transforms back](#reading-transforms-back) instead.

## Raycasting

`raycast` performs a single ray query against the physics world:

```python
import tempo_sim.TempoCore.Geometry_pb2 as Geometry
import tempo_sim.TempoWorld.WorldState_pb2 as WorldState

hit = tw.raycast(start=Geometry.Vector(x=0.0, y=0.0, z=2.0),
                 end=Geometry.Vector(x=50.0, y=0.0, z=2.0),
                 collision_channel=WorldState.CC_VISIBILITY,
                 ignored_actors=["MyActor"])

if hit.hit:
    print(hit.actor, hit.component, hit.distance_m, hit.location)
```

Start and end are in meters in the right-handed world frame, as is the returned `location` and
`normal`; `distance_m` is measured from `start`. `collision_channel` selects the query channel
(`CC_WORLD_STATIC`, `CC_WORLD_DYNAMIC`, `CC_VISIBILITY`), and `ignored_actors` excludes actors
from the query by name — usually the actor casting the ray.

## Spawning and destroying actors

To spawn, name the `actor_type` — any C++ or Blueprint class in the project, per
[Class names](../concepts/naming.md#classes). You may also give a transform and an actor to
interpret it relative to. The transform's `scale` is applied as given, never composed through that
actor ([Scale is unitless](../concepts/conventions.md#scale-is-unitless)).

A **deferred** spawn creates the actor in an invisible, unfinished state so you can set its
properties before finishing:

```python
import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

t = Geometry.Transform()
t.location.x = 1
spawn = tw.spawn_actor(actor_type="MyCPPOrBPActorClass", deferred=True,
                       transform=t, relative_to_actor="SomeOtherActor")
# The actor exists but is invisible to all others — set any custom properties now
tw.finish_spawning_actor(actor=spawn.name)
```

Both RPCs return a transform. If spawning at the transform you gave would have collided with
another actor, Unreal looks for a nearby transform instead — the returned one is where the actor
actually is.

```python
tw.destroy_actor(actor="ActorToDestroy")
```

## Adding and destroying components

Components can be added and removed in the Editor or at runtime:

```python
tw.add_component(component_type="MyCPPOrBPComponentClass", actor="OwnerActor",
                 name="OptionalCustomName", parent="OptionalParentComponent",
                 transform=t, socket="OptionalSocket")
tw.destroy_component(actor="OwnerActor", component="MyComponent")
```

A component can also be switched on and off without destroying it, which is cheaper than
destroying and re-adding one — and is how you stop a sensor without losing its configuration:

```python
tw.deactivate_component(actor="BP_SensorRig", component="TempoCamera")
tw.activate_component(actor="BP_SensorRig", component="TempoCamera")
```

## Moving actors and components

```python
t = Geometry.Transform()
t.location.x = 1.0
tw.set_actor_transform(actor="MyActor", transform=t, relative_to_actor="OptionalRelativeActor")
tw.set_component_transform(actor="OwnerActor", component="MyComponent",
                           transform=t, relative_to_world=False)
```

### These are partial updates

A member you don't set is left exactly as it was. `transform.location`, `transform.rotation` and
`transform.scale` are honored independently, so the example above moves `MyActor` without turning
or resizing it. Setting nothing at all is a no-op.

```python
r = Geometry.Transform()
r.rotation.y = 1.57
tw.set_actor_transform(actor="MyActor", transform=r)                         # turn in place
tw.set_actor_transform(actor="MyActor",
                       transform=Geometry.Transform(scale=Geometry.Vector(x=2.0, y=2.0, z=2.0)))  # resize in place
```

The requests' separate `scale` field is deprecated: it is honored only when `transform.scale` is
unset, and setting both is an error.

!!! important "Presence decides what gets applied, not value"

    Zero is a real request: a location of `(0, 0, 0)` moves the actor to the origin, and a
    rotation of `(0, 0, 0)` turns it to identity. What marks a member as supplied is *assigning*
    to it, even to `0.0` — so in Python `t.location.x = 0.0` counts, while merely *reading*
    `t.location.x` does not. `SetInParent()`, `CopyFrom(Vector())`, or
    `Transform(location=Vector())` are explicit ways to say "the origin". In Rust the field is a
    plain `Option`; in C++ any `mutable_location()` call marks it present.

    The corollary is the one migration hazard: `set_actor_transform(actor="X",
    transform=Geometry.Transform())` used to reset an actor to the origin with identity rotation.
    It is now a no-op, because nothing was supplied. Spell the reset out if you meant it. See
    [Migrating to v0.3.0](../migration/v0.3.0.md).

### These place, they do not sweep

The physics body is teleported along with the transform, and the move is never blocked part-way
by geometry — so `set_actor_transform` means the same thing whether or not the thing you named
simulates.

That matters because a client generally cannot tell. Unreal's own default would move the
transform and leave the body behind, so a Chaos wheeled vehicle would silently spring back to
where its body was left while a kinematic pawn obeyed the same call.

World-space velocity is preserved, so moving something that was already moving does not stop
it — zero its velocity too if that is what you meant. Scale never involves a body, so
`set_actor_scale3d` and `set_component_scale3d` are unaffected.

### Single-part RPCs

For the common cases there are first-class RPCs that say the same thing more directly:

| RPC | Sets | Component equivalent |
| --- | --- | --- |
| `set_actor_location` | location only | `set_component_location` |
| `set_actor_rotation` | rotation only | `set_component_rotation` |
| `set_actor_scale3d` | scale only | `set_component_scale3d` |

```python
tw.set_actor_location(actor="MyActor", location=Geometry.Vector(x=1.0, y=2.0, z=3.0))
tw.set_actor_rotation(actor="MyActor", rotation=Geometry.Rotation(r=0.0, p=0.0, y=1.57))
tw.set_actor_scale3d(actor="MyActor", scale=Geometry.Vector(x=2.0, y=2.0, z=2.0))
```

Unlike the transform RPCs these are **not** partial: the one value each takes is required, and
omitting it is a `FAILED_PRECONDITION` rather than a silent move to the origin or a collapse to
zero scale. The actor RPCs take `relative_to_actor` and the component RPCs take
`relative_to_world`, exactly as the transform RPCs do.

`relative_to_actor` interprets the transform in the named actor's frame: the transform you pass is
applied first and the reference actor's transform second — the same order `spawn_actor` composes
in. Scale is never composed through it; see
[Scale is unitless](../concepts/conventions.md#scale-is-unitless).

The component RPCs refuse to retarget an actor's root component — it *is* the actor's
transform — and point you at the matching `set_actor_*` RPC instead.

## Reading transforms back

Every setter above has a getter, reporting in the same units, handedness and frames.

```python
got = tw.get_actor_transform(actor="MyActor")
got.transform.location.x    # meters, right-handed
got.transform.rotation.y    # radians, right-handed
got.transform.scale.x       # unitless

tw.get_actor_location(actor="MyActor").location
tw.get_actor_rotation(actor="MyActor").rotation
tw.get_actor_scale3d(actor="MyActor").scale
```

The response's separate `scale` field is deprecated; it holds the same value as `transform.scale`.

`get_actor_transform`, `get_actor_location` and `get_actor_rotation` take `relative_to_actor` and
divide out exactly the frame their setters compose in, so a value read in a frame can be handed
straight back to the matching setter in that frame. `get_actor_scale3d` has no `relative_to_actor`,
because `set_actor_scale3d` has none either — and for the same reason, `get_actor_transform`'s
scale is always the world scale, whatever frame the pose is reported in.

### Choosing a component's frame

The component getters replace the setters' `relative_to_world` flag with a `frame`, because there
are more than two useful answers. It defaults to `CF_WORLD`, so asking for nothing asks for the
world transform.

| `frame` | Reports the component relative to | Setter counterpart |
| --- | --- | --- |
| `CF_WORLD` (default) | the world | `relative_to_world=True` |
| `CF_PARENT` | its attach parent — the transform Unreal actually stores | `relative_to_world=False` |
| `CF_ACTOR` | the owning actor | — |
| `CF_COMPONENT` | another component on the same actor, optionally at one of its sockets | — |

```python
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

# World space, the default.
tw.get_component_location(actor="OwnerActor", component="MyComponent")

# What set_component_location(relative_to_world=False) writes.
tw.get_component_transform(actor="OwnerActor", component="MyComponent",
                           frame=WorldControl.CF_PARENT)

# Where the component sits on its actor, whatever the attachment chain in between.
tw.get_component_location(actor="OwnerActor", component="MyComponent",
                          frame=WorldControl.CF_ACTOR)

# Relative to a socket on another component.
tw.get_component_transform(actor="OwnerActor", component="MyComponent",
                           frame=WorldControl.CF_COMPONENT,
                           relative_to_component="SkeletalMesh", socket="hand_r")
```

`CF_PARENT` already accounts for the socket a component is *attached* to, which is what makes
`socket` a `CF_COMPONENT` field rather than a free-floating one: reach for it when you want a
frame the component is not attached to. `relative_to_component` is required under `CF_COMPONENT`
and rejected under every other frame, rather than being quietly ignored.

A component's `scale` **does** follow `frame`, matching `set_component_scale3d`, whose scale
honors `relative_to_world`. So a component scaled 3x inside an actor scaled 2x reads as 3 under
`CF_PARENT` and 6 under `CF_WORLD`.

Unlike the setters, the component getters will address an actor's **root** component: reading it
is unambiguous in a way that retargeting it is not. It reports the actor's own transform under
`CF_WORLD`, and identity under `CF_ACTOR`.

## Getting and setting properties

TempoWorld uses Unreal's reflection system to get or set the value of any `UPROPERTY` by name.
When you don't know the names up front, ask:

```python
for actor in tw.get_all_actors().actors:
    for component in tw.get_all_components(name=actor.name).components:
        for prop in tw.get_component_properties(name=component.name).properties:
            print(f"{prop.actor}.{prop.component}.{prop.name}: {prop.type} = {prop.value}")
```

Or, more directly:

```python
tw.get_actor_properties(name="MyActor", include_components=True)
```

Each property's `value` is a human-readable string, Unreal-native like everything below: a vector
reads as `{X:100.000000, Y:0.000000, Z:0.000000}` in centimeters, a rotator in degrees as stored,
and a color with its alpha, an `FLinearColor` as linear floats. Read a value you mean to act on with
`get_property`, which returns it typed.

!!! tip "There is a typed alternative"

    Everything below addresses properties by string, which nothing can check until the call runs. If
    you are writing scenarios that have to survive a new release of the simulation, see
    [Reflected API](../clients/reflected-api.md): the same capability with the strings replaced by
    generated types, so a renamed or removed property is a type error rather than a runtime failure.
    This page remains the complete and unfiltered surface, and the reflected API is built on it.

Once you know the actor, component and property, write it with `set_property` and read it with
`get_property`. The value travels as a `Value` message, whose variant carries its type:

```python
from tempo_sim import tempo_world
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

tempo_world.set_property(actor="MyActor", property="MaxSpeed",
                         value=WorldControl.Value(float_value=42.0))
speed = tempo_world.get_property(actor="MyActor", property="MaxSpeed").float_value
```

| `Value` variant | Unreal type |
| --- | --- |
| `bool_value` | `bool` |
| `string_value` | `FString`, `FName`, or `FText` |
| `enum_value` | `TEnumAsByte` or raw enum, by name |
| `int_value` | `int32` (also `int8`/`int16`/`uint16` with range check, and `TEnumAsByte`) |
| `int64_value` | `int64` (also `uint32`/`uint64` with range check) |
| `float_value` | `double` or `float`, carried at double precision either way |
| `vector_value` | `FVector` |
| `vector2d_value` | `FVector2D` |
| `int_vector_value` | `FIntVector` |
| `int_point_value` | `FIntPoint` |
| `rotator_value` | `FRotator` |
| `quat_value` | `FQuat` |
| `native_transform_value` | `FTransform` |
| `color_value` | `FColor`, or `FLinearColor` given in 8-bit sRGB; an unset alpha `a` means 255 |
| `linear_color_value` | `FLinearColor` |
| `class_value` | `UClass*`, `TSubclassOf`, or `TSoftClassPtr` (by name) |
| `asset_value` | asset, `TSoftObjectPtr` (by path/name); also `TWeakObjectPtr` |
| `actor_value` | `AActor*` (by name) |
| `component_value` | `UActorComponent*` (by name, as `ActorName:ComponentName`) |
| `struct_value` | any other `USTRUCT`; see [Struct values](#struct-values) |

The scalar and reference kinds also have `*_array_value` and `*_set_value` variants, either of which
replaces the entire contents of a `TArray` or `TSet` property; a read reports the one matching the
property's container. `struct_array_value` carries a `TArray` of structs.

Nested addressing — `MyStruct.Inner`, `MyArray[0]`, `MyMap[key]`, to arbitrary depth — is
described under [Naming → Properties](../concepts/naming.md#properties).

!!! warning "Values are Unreal-native"

    Nothing is unit-converted in either direction: a distance is in centimeters, a rotator in
    degrees, and every vector left-handed, exactly as the property stores it. See
    [the exception](../concepts/conventions.md#the-exception-set_property).

!!! note "The typed setters are deprecated"

    The per-type RPCs — `set_bool_property`, `set_float_property`, `set_rotator_property` and the
    rest, with their `_array` and `_set` forms — are deprecated in v0.4.0 and will be removed in a
    later release. Each is `set_property` with one `Value` variant, except that the rotator,
    quaternion and transform setters convert from the API's units. See the
    [v0.4.0 migration guide](../migration/v0.4.0.md).

### Struct values

A `USTRUCT` with no first-class variant travels as a `struct_value`: its C++ name and its members by
authored name. `get_property` returns one for a struct property, `call_function` accepts one for a
struct parameter and returns one for a struct result, and `struct_array_value` carries a `TArray` of
them.

```python
value = WorldControl.Value()
value.struct_value.struct_name = "FBodyInstance"
field = value.struct_value.fields.add()
field.name = "LinearDamping"
field.value.float_value = 0.75
tempo_world.set_property(actor="MyActor", component="Mesh", property="BodyInstance", value=value)
```

- **A write is a merge.** Only the members present are written; the rest keep what they held — the
  property's current value, or a function parameter's default. The example above changes
  `LinearDamping` and nothing else.
- **A read reports every member**, inherited ones included.
- **Only structs whose every member can travel** are carried this way. One holding a map, a
  fixed-size array or another such struct is still reachable a member at a time, by nested address.
  `DescribeClasses` reports which is which as `StructDescriptor.value_expressible`.
- `struct_name` is checked: writing an `FHitResult` into an `FBodyInstance` property fails. Leave it
  empty to skip the check.

### Subsystems

`get_property`, `get_properties`, `stream_property`, `set_property` and `call_function` — and so
the ops in a `run_batch` — can address a subsystem instead of an actor: give `subsystem`, its class
by path or name, and leave `actor` and `component` empty. The instance is the one belonging to the
sim's game world: its own for a world subsystem, its game instance's, the engine's, or its first
local player's.

```python
offset = tempo_world.get_property(subsystem="/Script/TempoAgents.TempoAgentsWorldSubsystem",
                                  property="BrightnessMeterLateralOffset")
playing = tempo_world.call_function(subsystem="/Script/Engine.ReplaySubsystem", function="IsPlaying")
```

A class that is not a subsystem, or a subsystem that does not exist in this world, is `NOT_FOUND`;
naming an actor as well is `FAILED_PRECONDITION`. The deprecated per-type `set_*_property` RPCs
address actors only.

### Running several operations in order

`run_batch` carries property writes, property reads and function calls together, each the same
request its own RPC takes, and runs them in order in one go on the game thread — so the whole batch
happens between two ticks, and a read sees every write before it in the same batch.

```python
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

response = tempo_world.run_batch(stop_on_failure=True, ops=[
    WorldControl.BatchOp(set=WorldControl.SetPropertyRequest(
        actor="MyActor", property="bCanBeDamaged", value=WorldControl.Value(bool_value=False))),
    WorldControl.BatchOp(get=WorldControl.GetPropertyRequest(actor="MyActor", property="bCanBeDamaged")),
    WorldControl.BatchOp(call=WorldControl.CallFunctionRequest(actor="MyActor", function="ActorHasTag",
        args=[WorldControl.FunctionArg(name="Tag", value=WorldControl.Value(string_value="x"))])),
])
read_back = response.results[1].value.bool_value
```

There is one result per op, failed or not, because a read's value is found by position. With
`stop_on_failure`, the first failure ends the batch and every later op comes back `ABORTED` without
having run; without it, every op runs regardless. The
[reflected API](../clients/reflected-api.md#one-call-instead-of-twenty) is built on this.

`set_properties`, the older batch of typed writes, and the `batch()` builder over it are deprecated
in v0.4.0. `run_batch` with set ops does everything they do.

## Calling functions

`call_function` invokes any `UFUNCTION` on an actor or component by name. Arguments are typed
`Value`s, exactly as `set_property` takes — so a function argument follows the same rules as the
matching property type, including how classes, assets, actors and components are named, and the
same nested addressing for reaching into a parameter.

Rather than build `Value` messages by hand, the generated clients include a fluent `Call` builder
with one `*_arg` method per supported type:

=== "Python"

    ```python
    from tempo_sim import tempo_world

    tempo_world.call(actor="MyActor", function="ApplyDamage") \
        .float_arg("Amount", 42.5) \
        .vector_arg("Location", x=0.0, y=0.0, z=150.0) \
        .enum_arg("DamageType", "Explosive") \
        .actor_arg("Instigator", "BP_Player_C_0") \
        .execute()
    ```

=== "Rust"

    ```rust
    use tempo_sim::tempo_world;

    tempo_world::call("MyActor".into(), "".into(), "ApplyDamage".into())
        .float_arg("Amount".into(), 42.5)
        .vector_arg("Location".into(), 0.0, 0.0, 150.0)
        .enum_arg("DamageType".into(), "Explosive".into())
        .actor_arg("Instigator".into(), "BP_Player_C_0".into())
        .execute()?;
    ```

=== "C++"

    ```cpp
    #include <tempo.h>

    auto result = tempo::tempo_world::call("MyActor", "", "ApplyDamage")
        .float_arg("Amount", 42.5)
        .vector_arg("Location", 0.0, 0.0, 150.0)
        .enum_arg("DamageType", "Explosive")
        .actor_arg("Instigator", "BP_Player_C_0")
        .execute();
    ```

`execute_async` is available for `await` in Python and Rust. In Rust, `.subsystem(...)` on the
builder addresses a [subsystem](#subsystems) instead, with the actor and component left empty. Each
`*_arg` method fills one `Value` variant, so an argument is Unreal-native exactly as a property
value is: `vector_arg` takes centimeters, and a rotator argument takes degrees.

A few rules worth knowing:

- **Every input parameter must be supplied.** Blueprint parameter defaults live in editor-only
  metadata, so a packaged sim can't honor them; requiring all inputs means a call behaves the
  same in the Editor and in a packaged build. Out parameters and the return value must *not* be
  supplied. Naming part of a parameter (`"MyStruct.Inner"`) counts as supplying it, and the rest
  of that parameter keeps its zero value.
- **Return values and out parameters come back typed.** The response carries one
  `FunctionResult` per value the function handed back — its return value (which Unreal names
  `ReturnValue`) and any out parameters, in declaration order. `typed_value` is the same `Value`
  `get_property` returns, Unreal-native; `property_type` and the human-readable `value` string use
  the same vocabulary and format as `get_*_properties`.
- **Latent functions, C-style array parameters, and delegate parameters are rejected**, since
  there is no meaningful way to supply them over the API.

Functions with no parameters are called the same way, with no `*_arg` calls — or with the plain
RPC:

```python
tempo_world.call_function(actor="MyActor", function="ResetToStart")
```

Reading a result:

```python
response = tempo_world.call(actor="MyActor", function="GetDistanceTo") \
    .actor_arg("OtherActor", "BP_Player_C_0") \
    .execute()

for result in response.results:
    print(f"{result.name} ({result.property_type}) = {result.value}")
# ReturnValue (float) = 500.0
distance_cm = response.results[0].typed_value.float_value
```

## Discovering functions

Just as `get_*_properties` lets you find a property without knowing its name up front,
`get_actor_functions` and `get_component_functions` list the `UFUNCTION`s you can call:

```python
for function in tempo_world.get_actor_functions(actor="MyActor",
                                                include_components=True).functions:
    print(f"{function.signature}{'' if function.callable else '  # ' + function.error}")
# void SetActorHiddenInGame(bool bNewHidden)
# float GetDistanceTo(AActor* OtherActor)
# void GetActorBounds(bool bOnlyCollidingComponents, out vector Origin, out vector BoxExtent, bool bIncludeFromChildActors)
```

`signature` reads like the declaration does, and its type names come from the same vocabulary as
`property_type` in `get_*_properties` — so the type in a signature tells you which `*_arg` method
to call. Out parameters stay in the parameter list, prefixed with `out`, because that is where
`CallFunction`'s parameter frame expects them.

The same information is available split up, in `parameters`, which is what you want if you are
building a call programmatically rather than reading it:

| Field | Meaning |
| --- | --- |
| `name` | Parameter name, spelled the way an `*_arg` method's first argument expects it |
| `property_type` | Same vocabulary as `get_*_properties`' `property_type` |
| `kind` | `PK_INPUT` (you must supply it), `PK_OUTPUT`, or `PK_RETURN` (you must not) |

`callable` is false for the functions `call_function` would refuse — latent functions, and those
with C-style array or delegate parameters — with `error` saying why, so you never have to guess
which of the listed functions can actually be invoked. Delegate signatures aren't listed at all;
they're declared like functions but exist only to type a delegate.

## API reference

[:octicons-arrow-right-24: `TempoWorld` RPCs](../reference/api/tempo-world.md)
