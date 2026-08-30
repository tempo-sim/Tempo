# TempoWorld
`TempoWorld` includes features to query and control the state of the simulated world.

## Units and Coordinate Conventions
Unreal uses centimeters, degrees, and a left-handed coordinate system natively. `TempoWorld` automatically converts to and from this convention to meters, radians, and a right-handed coordinate system in all of its API calls, with one notable exception (see note at the bottom of [Getting and Setting Properties](#getting-and-setting-properties)).

## Actor and Component Names
`TempoWorld` uses the names of Actors and Components to refer to these entities. The name of an Actor is always the result of `AActor::GetActorNameOrLabel`, and the name of a Component is always the result of `UActorComponent::GetName`. The resulting behavior is:
- In Unreal Editor, Actor names match the labels in the World outliner. When Actors are spawned manually or otherwise they are automatically given a unique label. However, Unreal Editor will not stop you from manually assigning the same label to multiple Actors. In this case, the `TempoWorld` API will act on the first matching Actor it finds.
- In the packaged binary, Actors are assigned a unique name that is **not** the same as the label in the Editor. That means you **cannot** rely on hard-coded Actor names from the Editor in your client code. You should use the API to gather the Actors and Components and find the name of the Actor you need from the responses.
Name matching via `TempoWorld`'s APIs is **not** case sensitive*. 

## Class Names
`TempoWorld` refers to Unreal Classes by name too, in `spawn_actor`, `add_component`, and the Class-valued property setters. The name of a Class is always the result of `UClass::GetName`, which for a Blueprint Class includes Unreal's generated `_C` suffix (a `BP_MyActor` Blueprint generates a Class named `BP_MyActor_C`). That is the name `TempoWorld` reports in `actor_type`, `component_type`, and Class property values, so any of those can be passed straight back in. When you name a Class yourself you may use either form: `BP_MyActor` and `BP_MyActor_C` both resolve.

## World State
TempoWorld supports querying the state of Actors in the World. This includes RPCs to get (right now) or stream (continuously) the state of Actors, where the state includes the name, transform, 6-DoF velocity (linear m/s, angular rad/s), 3D bounds, and the timestamp when the state was captured. For example:
```
import tempo_sim.tempo_world as tw

# Get MyActor's state right now
tw.get_current_actor_state(actor="MyActor")
# Start a stream of MyActor's state
for state in tw.stream_actor_state(actor="MyActor"):
```
Often you may be interested in the states not of all Actors but only the ones near the one you are controlling or collecting data from. For this reason, TempoWorld includes RPCs to get or stream the states of all Actors *near* another Actor. For example:
```
import tempo_sim.tempo_world as tw

# Get the state of all Actors within 50 meters of MyActor (including MyActor itself) right now
tw.get_current_actor_states_near(near_actor="MyActor", search_radius_m=50.0)
# Start a stream of all such Actor states
for state in tw.stream_actor_states_near(near_actor="MyActor", search_radius_m=50.0):
```
You may also be interested in knowing if one Actor has overlapped another. `TempoWorld` has a streaming RPC for this. For example:
```
import tempo_sim.tempo_world as tw

for overlap_event in tw.stream_overlap_events(actor="MyActor"):
```

## Actor, Component, and Property Control
TempoWorld lets you control the state of the simulated world.

### Spawning or Destroying Actors
To spawn an Actor you must specify its `actor_type` (Unreal Class name, see [Class Names](#class-names)). This can be any C++ or Blueprint class in the project. You may also specify a transform and, optionally, an other actor to which that transform is relative. Lastly, you can specify that the spawn should be "deferred", meaning the Actor will be created but left in an invisible, unfinished state where you can set its properties before finishing the spawn. For example:
```
import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

t = Geometry.transform()
t.location.x = 1
spawn_response = tw.spawn_actor(actor_type="MyCPPOrBPActorClass", deferred=True, transform=t, relative_to_actor="SomeOtherActor")
# The Actor exists but is invisible to all others - this is your chance to supply any custom properties
finish_spawn_response = tw.finish_spawning_actor(actor=spawn_response.name)
```
Note that both the spawn actor and finish spawning actor RPCs return a transform. If spawning at the transform you provided would have resulted in a collision with another Actor Unreal will attempt to find a new transform nearby to spawn your new Actor. This is the transform that will be returned.

You can also destroy any Actor in the world by name. For example:
```
import tempo_sim.tempo_world as tw

tw.destroy_actor(actor="ActorToDestroy")
```

### Adding and Destroying Components
TempoWorld supports adding and removing components in the editor or at runtime. For example:
```
import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

t = Geometry.transform()
tw.add_component(component_type="MyCPPOrBPComponentClass", actor="OwnerActor", name="OptionalCustomName", parent="OptionalParentComponent", transform=t, socket="OptionalSocket")
tw.destroy_component(actor="OwnerActor", component="MyComponent")
```

### Manipulating Actors and Components
TempoWorld supports setting transforms of Actors and Components. For example:
```
import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

t = Geometry.Transform()
t.location.x = 1.0
tw.set_actor_transform(actor="MyActor", transform=t, relative_to_actor="OptionalRelativeActor")
tw.set_component_transform(actor="OwnerActor", component="MyComponent", transform=t, relative_to_world=False) # relative_to_world defaults to False, aka relative to parent
```

**These are partial updates.** A member you don't set is left exactly as it was. `transform.location`
and `transform.rotation` are honored independently, and scale rides alongside in its own `scale`
field, so the example above moves `MyActor` without turning or resizing it. Setting nothing at all
is a no-op. Every combination works:

```
r = Geometry.Transform()
r.rotation.y = 1.57
tw.set_actor_transform(actor="MyActor", transform=r)                                  # turn in place
tw.set_actor_transform(actor="MyActor", transform=Geometry.Transform(),
                       scale=Geometry.Vector(x=2.0, y=2.0, z=2.0))                    # resize in place
```

For the common single-part cases there are first-class RPCs, which say the same thing more directly:

| RPC | Sets | Component equivalent |
| --- | --- | --- |
| `set_actor_location` | location only | `set_component_location` |
| `set_actor_rotation` | rotation only | `set_component_rotation` |
| `set_actor_scale3d` | scale only | `set_component_scale3d` |

```
tw.set_actor_location(actor="MyActor", location=Geometry.Vector(x=1.0, y=2.0, z=3.0))
tw.set_actor_rotation(actor="MyActor", rotation=Geometry.Rotation(r=0.0, p=0.0, y=1.57))
tw.set_actor_scale3d(actor="MyActor", scale=Geometry.Vector(x=2.0, y=2.0, z=2.0))
```

Unlike the transform RPCs, these are not partial: the one value each takes is required, and
omitting it is a `FAILED_PRECONDITION` rather than a silent move to the origin or a collapse to
zero scale. The actor RPCs take `relative_to_actor` and the component RPCs take `relative_to_world`,
exactly as the transform RPCs do.

> [!NOTE]
> **Scale is unitless and unconverted.** Every other `Vector` in this API is meters/right-handed
> and is converted on the way in; a scale is a ratio, so its components are used exactly as given.
> Negating Y to change handedness would mirror the object rather than re-express it.

> [!NOTE]
> **Scale is never composed through `relative_to_actor`.** Location and rotation are interpreted in
> the reference Actor's frame; scale is always absolute. A non-uniform scale composed through a
> rotated frame does not survive as a transform, so applying the frame to it would quietly produce
> something the caller did not ask for.

`relative_to_actor` interprets the transform in the named Actor's frame: the transform you pass is
applied first and the reference Actor's transform second, the same order `spawn_actor` composes in.

The component RPCs refuse to retarget an Actor's root component - it *is* the Actor's transform -
and point you at the matching `set_actor_*` RPC instead.

### Getting and Setting Properties
TempoWorld uses Unreal's reflection system to allow getting or setting the value of any UProperty by name. Sometimes you might not know the exact name of the Actor, Component, or property you want to set at runtime. For this reason RPCs to inspect properties are available. For example:
```
import tempo_sim.tempo_world as tw

all_actors_response = tw.get_all_actors()
for actor in all_actors_response.actors:
  all_components_response = tw.get_all_components(name=actor.name)
  for component in all_components_response.components:
    all_properties_response = tw.get_component_properties(name=component.name)
    for property in all_properties_response.properties:
      print("Property: [Actor: {} Component: {} Name: {} Type: {} Value: {}]".format(property.actor, property.component, property.name, property.type, property.value)
```
For convenience, you can also get the properties of an Actor, optionally including those of its components, with:
```
tw.get_actor_properties(name="MyActor", include_components=True)
```
Once you do know the name of the Actor, Component, and Property you want to set you can do so with the corresponding set property RPC. There is one of these for each type you may want to set. `TempoWorld` currently supports setting the following types at runtime:

| Set Property Type    | Unreal Type |
| ------------- | ------------- |
| `set_bool_property`          | bool              |
| `set_string_property`        | FString, FName, or FText |
| `set_enum_property`          | TEnumAsByte or raw Enum |
| `set_int_property`           | int32 (also accepts int8/int16/uint16 with range check, and TEnumAsByte) |
| `set_int64_property`         | int64 (also accepts uint32/uint64 with range check) |
| `set_float_property`         | double or float   |
| `set_vector_property`        | FVector           |
| `set_vector2d_property`      | FVector2D         |
| `set_int_vector_property`    | FIntVector        |
| `set_int_point_property`     | FIntPoint         |
| `set_rotator_property`       | FRotator          |
| `set_quat_property`          | FQuat             |
| `set_transform_property`     | FTransform        |
| `set_color_property`         | FColor or FLinearColor           |
| `set_class_property`         | UClass*, TSubclassOf, or TSoftClassPtr (by name) |
| `set_asset_property`         | Asset, TSoftObjectPtr (by path/name) |
| `set_actor_property`         | AActor* (by name) |
| `set_component_property`     | UActorComponent* (by name, as `ActorName:ComponentName`) |

All of the above (except the struct types `vector`, `vector2d`, `int_vector`, `int_point`, `rotator`, `quat`, `transform`, and `color`) also have `set_*_array_property` and `set_*_set_property` RPCs to replace the entire contents of a `TArray` or `TSet` property in one call.

TempoWorld also supports setting individual properties in structs, arrays, and maps, with the syntax `MyStruct.InnerProperty`, `MyArray[0]`, and `MyMap[key]`, respectively. It also supports arbitrarily deep nesting of properties. Array indices must be either already present in the array or one past the length of the array (to extend the array by one element). Map keys that don't yet exist are inserted.

`TSet` element-level addressing is not supported (a set element is its own identity, so there's nothing to address into); use `set_*_set_property` to replace a set's contents.

> [!Warning]
> While the values you set for rotators and quaternions will be converted from radians/right-handed to degrees/left-handed, the values you set for floats and vector-shaped types (`vector`, `vector2d`, `int_vector`, `int_point`) will not be converted (so, these should be specified in centimeters if they represent distances). We don't feel it is safe to assume that these quantities always represent distances. `set_transform_property` is the one exception: it follows the same convention as `spawn_actor` and `set_actor_transform`, converting the location from meters to centimeters and the rotation from radians/right-handed to degrees/left-handed, since transforms almost always represent world-frame poses.

Every one of the RPCs above is a named shorthand for the same underlying operation: write one typed
value into one property. That operation is also available directly as `set_property`, which carries
the value's type in a `Value` message instead of in the RPC name:

```
from tempo_sim import tempo_world
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

tempo_world.set_property(actor="MyActor", property="MaxSpeed",
                         value=WorldControl.Value(float_value=42.0))
```

Reach for it when the type isn't known until runtime — dispatching over a config file, or replaying
recorded property values. When you do know the type at the call site, the named `set_*_property`
functions say the same thing more plainly.

### Batching Property Sets
When you need to apply multiple property changes together, the `set_properties` RPC accepts a list of any of the singular set-property ops above and runs them in order in a single call. To avoid building op messages by hand, the generated clients include a fluent `Batch` builder with one method per supported type, named after its singular counterpart. The response contains one entry per *failed* op (by index); an empty `failures` list means every op succeeded.

In Python:
```
from tempo_sim import tempo_world

response = (
    tempo_world.batch()
        .set_bool_property(actor="MyActor", property="bEnabled", value=True)
        .set_float_property(actor="MyActor", property="MaxSpeed", value=42.0)
        .set_vector_property(actor="MyActor", component="Mesh", property="RelativeLocation", x=0.0, y=0.0, z=150.0)
        .set_string_array_property(actor="MyActor", property="WaypointTags", values=["alpha", "bravo", "charlie"])
        .execute()
)

for failure in response.failures:
    print(f"op {failure.op_index} failed (code={failure.code}): {failure.error}")
```
Each `set_*_property` method takes the same keyword args as the corresponding free function, and `execute_async` is available for use with `await`.

In Rust:
```
use tempo_sim::tempo_world;

let response = tempo_world::batch()
    .set_bool_property("MyActor".into(), "".into(), "bEnabled".into(), true)
    .set_float_property("MyActor".into(), "".into(), "MaxSpeed".into(), 42.0)
    .set_vector_property("MyActor".into(), "Mesh".into(), "RelativeLocation".into(), 0.0, 0.0, 150.0)
    .set_string_array_property("MyActor".into(), "".into(), "WaypointTags".into(),
        vec!["alpha".into(), "bravo".into(), "charlie".into()])
    .execute()?;

for failure in &response.failures {
    eprintln!("op {} failed (code={}): {}", failure.op_index, failure.code, failure.error);
}
```
Args are positional and match the request message's field order (`actor, component, property, value/values/...`), the same as the singular `tempo_world::set_*_property` wrappers. `execute_async().await` is available for async callers.

### Calling Functions
`call_function` invokes any `UFUNCTION` on an Actor or Component by name. Arguments are typed
`Value`s, exactly as `set_property` takes — so a function argument follows the same rules as the
matching property type, including how classes, assets, actors, and components are named, and the
same nested addressing (`MyStruct.Inner`, `MyArray[0]`, `MyMap[key]`) for reaching into a
parameter.

Rather than build `Value` messages by hand, the generated clients include a fluent `Call` builder
with one `*_arg` method per supported type, named after the type it carries. In Python:

```
from tempo_sim import tempo_world

tempo_world.call(actor="MyActor", function="ApplyDamage") \
    .float_arg("Amount", 42.5) \
    .vector_arg("Location", x=0.0, y=0.0, z=150.0) \
    .enum_arg("DamageType", "Explosive") \
    .actor_arg("Instigator", "BP_Player_C_0") \
    .execute()
```

In Rust:

```
use tempo_sim::tempo_world;

tempo_world::call("MyActor".into(), "".into(), "ApplyDamage".into())
    .float_arg("Amount".into(), 42.5)
    .vector_arg("Location".into(), 0.0, 0.0, 150.0)
    .enum_arg("DamageType".into(), "Explosive".into())
    .actor_arg("Instigator".into(), "BP_Player_C_0".into())
    .execute()?;
```

In C++:

```
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
`vector_arg` takes `x`/`y`/`z` and `transform_arg` takes a whole `Transform`, matching
`set_vector_property` and `set_transform_property`. The same conversion caveats apply: rotators,
quaternions, and transforms are converted from the API's radians/right-handed convention, and
floats and the vector-shaped types are not.

A few rules worth knowing:

- **Every input parameter must be supplied.** Blueprint parameter defaults live in editor-only
  metadata, so a packaged sim can't honor them; requiring all inputs means a call behaves the same
  in the editor and in a packaged build. Out parameters and the return value must *not* be
  supplied. Naming part of a parameter (`"MyStruct.Inner"`) counts as supplying it, and the rest of
  that parameter keeps its zero value.
- **Return values and out parameters come back as strings.** The response carries one
  `FunctionResult` per value the function handed back — its return value (which Unreal names
  `ReturnValue`) and any out parameters, in declaration order. `property_type` and `value` use
  exactly the same vocabulary and string format as `get_*_properties`, since they share the same
  code, so a returned struct reads the same as that struct read back off a property. A void
  function with no out parameters returns an empty `results` list. Note this means results are
  Unreal-native and *not* unit-converted, the same way property values aren't.
- **Latent functions, C-style array parameters, and delegate parameters are rejected**, since
  there's no meaningful way to supply them over the API.

Functions with no parameters at all are called the same way, with no `*_arg` calls — or with the
plain `call_function` RPC:

```
tempo_world.call_function(actor="MyActor", function="ResetToStart")
```

Reading a result:

```
response = tempo_world.call(actor="MyActor", function="GetDistanceTo") \
    .actor_arg("OtherActor", "BP_Player_C_0") \
    .execute()

for result in response.results:
    print(f"{result.name} ({result.property_type}) = {result.value}")
# ReturnValue (float) = 500.0
```

### Discovering Functions
Just as `get_*_properties` lets you find a property without knowing its name up front,
`get_actor_functions` and `get_component_functions` list the `UFUNCTION`s you can call:

```
from tempo_sim import tempo_world

for function in tempo_world.get_actor_functions(actor="MyActor", include_components=True).functions:
    print(f"{function.signature}{'' if function.callable else '  # ' + function.error}")
# void SetActorHiddenInGame(bool bNewHidden)
# float GetDistanceTo(AActor* OtherActor)
# void GetActorBounds(bool bOnlyCollidingComponents, out vector Origin, out vector BoxExtent, bool bIncludeFromChildActors)
```

`signature` reads like the declaration does, and its type names come from the same vocabulary as
`property_type` in `get_*_properties` — so the type in a signature tells you which `*_arg` method
to call. Out parameters stay in the parameter list, prefixed with `out`, because that is where
`CallFunction`'s parameter frame expects them.

The same information is also available split up, in `parameters`, which is what you want if you're
building a call programmatically rather than reading it:

| Field | Meaning |
| ------------- | ------------- |
| `name`          | Parameter name, spelled the way an `*_arg` method's first argument expects it |
| `property_type` | Same vocabulary as `get_*_properties`' `property_type` |
| `kind`          | `PK_INPUT` (you must supply it), `PK_OUTPUT`, or `PK_RETURN` (you must not) |

`callable` is false for the functions `call_function` would refuse — latent functions, and those
with C-style array or delegate parameters — with `error` saying why, so you never have to guess
which of the listed functions can actually be invoked. Delegate signatures aren't listed at all;
they're declared like functions but exist only to type a delegate.
