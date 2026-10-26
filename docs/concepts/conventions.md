# Units and Coordinates

Unreal natively uses **centimeters**, **degrees**, and a **left-handed** coordinate system.
Robotics does not. Tempo converts between the two at the API boundary so your client code stays
in the convention it expects.

| | Unreal internal | Tempo API |
|---|---|---|
| Distance | centimeters | **meters** |
| Angle | degrees | **radians** |
| Handedness | left-handed | **right-handed** |

Velocities follow: linear in m/s, angular in rad/s.

This applies to actor and component transforms, world state, sensor poses, movement commands,
lidar azimuths and elevations, and geographic coordinates.

## The exception: property and function access { #the-exception-set_property }

The property and function RPCs in [TempoWorld](../plugins/tempo-world.md#getting-and-setting-properties)
— `set_property`, `get_property`, `get_properties`, `stream_property`, `call_function` and
`run_batch` — reach *arbitrary* `UPROPERTY` values and `UFUNCTION` parameters through Unreal's
reflection system. Tempo cannot know whether a given `float` or `FVector` represents a distance, a
ratio, a gain, or a color channel, so it converts nothing: every value they take or report is
**Unreal-native**, exactly as the property stores it.

| Value | Through the property and function RPCs |
|---|---|
| A distance, a location, a velocity | centimeters, left-handed |
| A rotator | `rotator_value`: roll, pitch, yaw in **degrees**, as Unreal's `FRotator` |
| A quaternion | `quat_value`: Unreal's own left-handed `FQuat` |
| A transform | `native_transform_value`: rotation as a quaternion, translation in centimeters, scale |
| A linear color | `linear_color_value`, unclamped floats |

!!! warning "The practical consequence"

    ```python
    # Meters -- set_actor_location is a first-class, unit-converted RPC.
    tw.set_actor_location(actor="MyActor", location=Geometry.Vector(x=1.0, y=2.0, z=3.0))

    # Centimeters -- RelativeLocation is a raw UProperty.
    tw.set_property(actor="MyActor", component="Mesh", property="RelativeLocation",
                    value=WorldControl.Value(vector_value=Geometry.Vector(x=100.0, y=200.0, z=300.0)))
    ```

The [reflected API](../clients/reflected-api.md#units) is built on these RPCs and is Unreal-native
for the same reason. Its value types are Unreal's own — `FVector`, `FRotator`, `FQuat`,
`FTransform` — shaped as Unreal's are. `tempo_sim.conversion` (Python), `tempo_sim::conversion`
(Rust) and `tempo/conversion.h` (C++) convert them to and from the API's units with the rules above:
`ue_to_si` and `si_to_ue`. An `FQuat` converts to a `Quat` exactly; for Euler angles, convert the
rotator Unreal would make of it, `ue_to_si(rotator_of(q))`.

??? note "The deprecated typed setters convert differently"

    The typed setters — `set_float_property`, `set_rotator_property` and the rest, deprecated in
    v0.4.0 — keep the rule they always had: a rotator, a quaternion and a transform are given in
    radians and right-handed, with a transform's location in meters, while a scalar or a vector is
    raw. Moving a call from one of them to `set_property` therefore changes the units of a rotator,
    a quaternion or a transform. See the [v0.4.0 migration guide](../migration/v0.4.0.md).

## Scale is unitless

`TempoCore.Transform` carries a `scale` beside its location and rotation. Where you give a
transform, leaving it unset means (1, 1, 1); a transform Tempo reports always has it set.

Every other `Vector` in the API is meters and right-handed, and is converted on the way in. A
scale is a ratio, so its components are used exactly as given — negating Y to change handedness
would mirror the object rather than re-express it.

Scale is also never composed through `relative_to_actor`. Location and rotation are interpreted
in the reference actor's frame; scale is always absolute. A non-uniform scale composed through a
rotated frame does not survive as a transform, so applying the frame to it would quietly produce
something you did not ask for.

## Lidar sign convention

`LidarScanSegment`'s `azimuths_rad` and `elevations_rad` are negated from Unreal's internal
left-handed, Z-down convention, so client-side point-cloud math renders right-handed Z-up
directly with no further correction.

## ROS conversions

The [TempoROS](../plugins/tempo-ros.md) converters do the same job for ROS message types. For
example, `FVector` ⇄ `geometry_msgs::msg::Vector3` scales by 0.01 and negates Y, converting from
Unreal's left-handed centimeters to ROS's right-handed meters.

## Geographic frames

Epic's GeoReferencing plugin is East-South-Up internally. Tempo composes a fixed −90° yaw
correction so that `OriginRotation` presents **North-West-Up**, as documented. See
[TempoGeographic](../plugins/tempo-geographic.md).
