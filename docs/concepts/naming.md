# Naming

The API addresses things by name — actors, components, classes, properties. Getting the naming
rules right avoids most "it works in the Editor but not in the packaged build" surprises.

## Actors

An actor's name is always the result of `AActor::GetActorNameOrLabel`.

=== "In the Editor"

    Actor names match the labels in the World Outliner. Actors spawned manually or otherwise are
    given a unique label automatically.

    Unreal will not stop you from assigning the same label to several actors. When that happens,
    the TempoWorld API acts on the **first matching actor it finds**.

=== "In a packaged build"

    Actors are assigned a unique name that is **not** the same as the Editor label.

!!! warning "Do not hard-code Editor actor names"

    Because the packaged names differ, client code that hard-codes a name from the Editor will
    break the moment you package. Query for actors and use the names from the responses:

    ```python
    import tempo_sim.tempo_world as tw

    for actor in tw.get_all_actors().actors:
        print(actor.name, actor.type)
    ```

Name matching is **not** case sensitive.

## Components

A component's name is always the result of `UActorComponent::GetName`. Components are addressed
relative to their owning actor:

```python
tw.get_component_properties(name="TempoCamera")
tw.set_float_property(actor="BP_SensorRig", component="TempoCamera",
                      property="FOVAngle", value=60.0)
```

When a property *value* refers to a component, the encoding is `ActorName:ComponentName`.

## Classes

Unreal classes are named by `UClass::GetName`, which for a Blueprint class includes Unreal's
generated `_C` suffix — a `BP_MyActor` Blueprint generates a class named `BP_MyActor_C`.

That is the name Tempo reports in `actor_type`, `component_type`, and class-valued property
values, so any of those can be passed straight back in. When *you* name a class, either form
works: `BP_MyActor` and `BP_MyActor_C` both resolve.

!!! tip "Use `GetActorIdentifier`, not `GetActorNameOrLabel`, in your own RPCs"

    If you write your own services that report actor names over the wire, use
    `UTempoCoreUtils::GetActorIdentifier`. The Editor's label handling can race in a way that
    adds or drops the `_C` suffix; `GetActorIdentifier` produces a stable identifier.

## Properties

Properties are named as declared in C++ or Blueprint — `FOVAngle`, `bEnabled`, `MaxSpeed`.

Nested addressing lets you reach inside structs, arrays and maps, to arbitrary depth:

| Syntax | Reaches |
|---|---|
| `MyStruct.InnerProperty` | a struct member |
| `MyArray[0]` | an array element |
| `MyMap[key]` | a map value |

Array indices must be either already present in the array, or exactly one past its end (which
extends it by one element). Map keys that don't exist yet are inserted. `TSet` elements cannot be
addressed individually — a set element is its own identity, so there is nothing to address into;
use `set_*_set_property` to replace the whole set.

Don't know the names? Ask:

```python
tw.get_actor_properties(name="MyActor", include_components=True)
tw.get_actor_functions(actor="MyActor", include_components=True)
```

## RPC names

Generated client function names are the RPC name converted to snake_case, with one rule worth
knowing: a capital that directly follows a digit continues the current word unless a lowercase
letter follows it.

| RPC | Client function |
|---|---|
| `SpawnActor` | `spawn_actor` |
| `SetActorScale3D` | `set_actor_scale3d` |
| `SetVector2DProperty` | `set_vector2d_property` |
| `SetInt64Property` | `set_int64_property` |

The service name does not appear in the client function name — nor does the file name or the
proto package. That brevity comes with one restriction: **RPC names must be unique within a
module.**

The same rule names generated packages, so a project directory with a digit followed by a capital
is affected too: `MyGame2D` publishes as `my-game2d` (import `my_game2d`).
