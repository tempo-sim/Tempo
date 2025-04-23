# TempoWorld
`TempoWorld` includes features to query and control the state of the simulated world.

## Units and Coordinate Conventions
Unreal uses centimeters, degrees, and a left-handed coordinate system natively. `TempoWorld` automatically converts to and from this convention to meters, radians, and a right-handed coordinate system in all of its API calls, with one notable exception (see note at the bottom of [Actor Control](#actor-control)).

## Actor and Component Names
`TempoWorld` uses the names of Actors and Components to refer to these entities. The name of an Actor is always the result of `AActor::GetActorLabelOrName`, and the name of a Component is always the result of `UActorComponent::GetName`. The resulting behavior is:
- In Unreal Editor, Actor names match the labels in the World outliner. When Actors are spawned manually or otherwise they are automatically given a unique label. However, Unreal Editor will not stop you from manually assigning the same label to multiple Actors. In this case, the `TempoWorld` API will act on the first matching Actor it finds.
- In the packaged binary, Actors are assigned a unique name that is **not** the same as the label in the Editor. That means you **cannot** rely on hard-coded Actor names from the Editor in your client code. You should use the API to gather the Actors and Components and find the name of the Actor you need from the responses.
Name matching via `TempoWorld`'s APIs is **not** case sensitive*. 

## World State
The world state service enables querying the state of Actors in the World. This includes RPCs to get (right now) or stream (continuously) the state of Actors, where the state includes the name, transform, linear and angular velocity, 3D bounds, and the timestamp when the state was captured. In a Python client, this could look like:
```
import tempo.tempo_world as tw

# Get MyActor's state right now
tw.get_current_actor_state(actor_name="MyActor")
# Start a stream of MyActor's state
for state in tw.stream_actor_state(actor_name="MyActor"):
```
Often you may be interested in the states not of all Actors but only the ones near the one you are controlling or collecting data from. For this reason, the world state service includes RPCs to get or stream the states of all Actors *near* another Actor. In a Python client, this could look like:
```
import tempo.tempo_world as tw

# Get the state of all Actors within 50 meters of MyActor (including MyActor itself) right now
tw.get_current_actor_states_near(near_actor_name="MyActor", search_radius=50.0)
# Start a stream of all such Actor states
for state in tw.stream_actor_states_near(near_actor_name="MyActor", search_radius=50.0):
```
You may also be interested in knowing if one Actor has overlapped another. `TempoWorld` has a streaming RPC for this.  In a Python client, this could look like:
```
import tempo.tempo_world as tw

for overlap_evnet in tw.stream_overlap_events(actor_name="MyActor"):
```

## Actor Control
The actor control service enables spawning and destroying Actors, as well as getting and setting properties of Actors and Components at runtime.

### Spawning Actors
To spawn an Actor you must specify its `type` (Unreal Class name). This can be any C++ or Blueprint class in the project. You may also specify a transform and, optionally, an other actor to which that transform is relative. Lastly, you can specify that the spawn should be "deferred", meaning the Actor will be created but left in an invisible, unfinished state where you can set its properties before finishign the spawn.  In a Python client, this could look like:
```
import tempo.tempo_world as tw
import TempoScripting.Geometry_pb2 as Geometry

t = Geometry.transform()
t.location.x = 1
spawn_response = tw.spawn_actor(type="MyCPPOrBPClass", deferred=True, transform=t, relative_to_actor="SomeOtherActor")
# The Actor exists but is invisible to all others - this is your chance to supply any custom properties
finish_spawn_response = tw.finish_spawning_actor(actor=spawn_response.spawned_name)
```
Note that both the spawn actor and finish spawning actor RPCs return a transform. If spawning at the transform you provided would have resulted in a collision with another Actor Unreal will attempt to find a new transform nearby to spawn your new Actor. This is the transform that will be returned.

### Getting and Setting Properties
TempoWorld uses Unreal's reflection system to allow getting or setting the value of any UProperty by name. Sometimes you might now know the exact name of the Actor, Component, or property you want to set at runtime. For this reason RPCs to inspect properties are available. For example, in a Python client you could gather the states of all properties, on all Components, on all Actors:
```
import tempo.tempo_world as tw

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
| `set_string_property`        | FString or FName  |
| `set_enum_property`          | TEnumAsByte or raw Enum |
| `set_int_property`           | int32             |
| `set_float_property`         | double or float   |
| `set_vector_property`        | FVector           |
| `set_rotator_property`       | FRotator          |
| `set_color_property`         | FColor or FLinearColor           |
| `set_class_property`         | UClass* or TSubclassOf (by name) |
| `set_asset_property`         | Asset (by name)   |
| `set_actor_property`         | AActor* (by name) |
| `set_component_property`     | UActorComponent* (by name)       |

All of the above also have `set_*_array_property` RPCs to set an array of such properties.

TempoWorld also supports setting indivudal properties in structs and arrays, with the syntax `MyStruct.InnerProperty` and `MyArray[0]`, respectively. It also supports arbitrarily deep nesting of properties. Array indices must be either already present in the array or one past the length of the array (to extend the array by one element).

TempoWorld does not yet support setting properties in maps or sets. Check back soon for that!

> [!Warning]
> While the values you set for rotators will be converted from radians to degrees, the values you set for floats and vectors will not be converted (so, these should be specified in centimeters if they represent distances). We don't feel it is safe to assume that these quantities always represent distances.

## Map Query Service

If your simulator includes a lane graph built with the ZoneGraph plugin, the map query service can get or stream the lane graph, including connectivity of lanes, as well as the accessibility of connected lanes (as determined by traffic controls).
