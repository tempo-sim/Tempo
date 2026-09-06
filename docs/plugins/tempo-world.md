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
timestamp when it was captured.

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

## Raycasting

`raycast` performs a single ray query against the physics world:

```python
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
interpret it relative to.

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

A member you don't set is left exactly as it was. `transform.location` and `transform.rotation`
are honored independently, and scale rides alongside in its own `scale` field — so the example
above moves `MyActor` without turning or resizing it. Setting nothing at all is a no-op.

```python
r = Geometry.Transform()
r.rotation.y = 1.57
tw.set_actor_transform(actor="MyActor", transform=r)                         # turn in place
tw.set_actor_transform(actor="MyActor", transform=Geometry.Transform(),
                       scale=Geometry.Vector(x=2.0, y=2.0, z=2.0))           # resize in place
```

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

Once you know the actor, component and property, set it with the RPC for its type:

| Set property RPC | Unreal type |
| --- | --- |
| `set_bool_property` | `bool` |
| `set_string_property` | `FString`, `FName`, or `FText` |
| `set_enum_property` | `TEnumAsByte` or raw enum |
| `set_int_property` | `int32` (also `int8`/`int16`/`uint16` with range check, and `TEnumAsByte`) |
| `set_int64_property` | `int64` (also `uint32`/`uint64` with range check) |
| `set_float_property` | `double` or `float` |
| `set_vector_property` | `FVector` |
| `set_vector2d_property` | `FVector2D` |
| `set_int_vector_property` | `FIntVector` |
| `set_int_point_property` | `FIntPoint` |
| `set_rotator_property` | `FRotator` |
| `set_quat_property` | `FQuat` |
| `set_transform_property` | `FTransform` |
| `set_color_property` | `FColor` or `FLinearColor` |
| `set_class_property` | `UClass*`, `TSubclassOf`, or `TSoftClassPtr` (by name) |
| `set_asset_property` | asset, `TSoftObjectPtr` (by path/name) |
| `set_actor_property` | `AActor*` (by name) |
| `set_component_property` | `UActorComponent*` (by name, as `ActorName:ComponentName`) |

All of the above except the struct types (`vector`, `vector2d`, `int_vector`, `int_point`,
`rotator`, `quat`, `transform`, `color`) also have `set_*_array_property` and
`set_*_set_property` RPCs, which replace the entire contents of a `TArray` or `TSet` in one call.

Nested addressing — `MyStruct.Inner`, `MyArray[0]`, `MyMap[key]`, to arbitrary depth — is
described under [Naming → Properties](../concepts/naming.md#properties).

!!! warning "Most of these are *not* unit-converted"

    Rotators, quaternions and transforms are converted from the API's radians/right-handed
    convention. Floats and the vector-shaped types are **not** — pass centimeters if they
    represent distances. See [the exception](../concepts/conventions.md#the-exception-set_property).

### `set_property`: one call, runtime types

Every RPC above is a named shorthand for the same underlying operation — write one typed value
into one property. That operation is also available directly, carrying the type in a `Value`
message instead of in the RPC name:

```python
from tempo_sim import tempo_world
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

tempo_world.set_property(actor="MyActor", property="MaxSpeed",
                         value=WorldControl.Value(float_value=42.0))
```

Reach for it when the type isn't known until runtime — dispatching over a config file, or
replaying recorded property values. When you know the type at the call site, the named
`set_*_property` functions say the same thing more plainly.

### Batching property sets

`set_properties` accepts a list of any of the singular set-property ops above and runs them in
order in a single call. Rather than build op messages by hand, the generated clients include a
fluent `Batch` builder with one method per supported type, named after its singular counterpart.

The response contains one entry per **failed** op, by index; an empty `failures` list means every
op succeeded.

=== "Python"

    ```python
    from tempo_sim import tempo_world

    response = (
        tempo_world.batch()
            .set_bool_property(actor="MyActor", property="bEnabled", value=True)
            .set_float_property(actor="MyActor", property="MaxSpeed", value=42.0)
            .set_vector_property(actor="MyActor", component="Mesh",
                                 property="RelativeLocation", x=0.0, y=0.0, z=150.0)
            .set_string_array_property(actor="MyActor", property="WaypointTags",
                                       values=["alpha", "bravo", "charlie"])
            .execute()
    )

    for failure in response.failures:
        print(f"op {failure.op_index} failed (code={failure.code}): {failure.error}")
    ```

    Each method takes the same keyword arguments as the corresponding free function, and
    `execute_async` is available for `await`.

=== "Rust"

    ```rust
    use tempo_sim::tempo_world;

    let response = tempo_world::batch()
        .set_bool_property("MyActor".into(), "".into(), "bEnabled".into(), true)
        .set_float_property("MyActor".into(), "".into(), "MaxSpeed".into(), 42.0)
        .set_vector_property("MyActor".into(), "Mesh".into(), "RelativeLocation".into(),
                             0.0, 0.0, 150.0)
        .set_string_array_property("MyActor".into(), "".into(), "WaypointTags".into(),
            vec!["alpha".into(), "bravo".into(), "charlie".into()])
        .execute()?;

    for failure in &response.failures {
        eprintln!("op {} failed (code={}): {}", failure.op_index, failure.code, failure.error);
    }
    ```

    Args are positional and match the request message's field order
    (`actor, component, property, value/values/...`), the same as the singular
    `tempo_world::set_*_property` wrappers. `execute_async().await` is available for async
    callers.

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
        .float_arg("Amount", 42.5f)
        .vector_arg("Location", 0.0, 0.0, 150.0)
        .enum_arg("DamageType", "Explosive")
        .actor_arg("Instigator", "BP_Player_C_0")
        .execute();
    ```

`execute_async` is available for `await` in Python and Rust. Each `*_arg` method takes the same
arguments as the corresponding `set_*_property` function, minus the property name — so
`vector_arg` takes `x`/`y`/`z` and `transform_arg` takes a whole `Transform`. The same conversion
caveats apply.

A few rules worth knowing:

- **Every input parameter must be supplied.** Blueprint parameter defaults live in editor-only
  metadata, so a packaged sim can't honor them; requiring all inputs means a call behaves the
  same in the Editor and in a packaged build. Out parameters and the return value must *not* be
  supplied. Naming part of a parameter (`"MyStruct.Inner"`) counts as supplying it, and the rest
  of that parameter keeps its zero value.
- **Return values and out parameters come back as strings.** The response carries one
  `FunctionResult` per value the function handed back — its return value (which Unreal names
  `ReturnValue`) and any out parameters, in declaration order. `property_type` and `value` use
  exactly the same vocabulary and string format as `get_*_properties`, since they share the same
  code. Results are Unreal-native and *not* unit-converted, the same way property values aren't.
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
