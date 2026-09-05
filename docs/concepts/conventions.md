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

## The exception: `set_*_property` { #the-exception-set_property }

The property setters in [TempoWorld](../plugins/tempo-world.md#getting-and-setting-properties)
reach *arbitrary* `UPROPERTY` values through Unreal's reflection system. Tempo cannot know
whether a given `float` or `FVector` represents a distance, a ratio, a gain, or a color channel —
so for those it does not guess.

| Property type | Converted? |
|---|---|
| `set_rotator_property`, `set_quat_property` | :material-check: radians/right-handed → degrees/left-handed |
| `set_transform_property` | :material-check: location m → cm, rotation converted |
| `set_float_property` | :material-close: raw — pass centimeters if it is a distance |
| `set_vector_property`, `set_vector2d_property`, `set_int_vector_property`, `set_int_point_property` | :material-close: raw |

`set_transform_property` is converted because a transform almost always represents a world-frame
pose. A bare float or vector does not.

!!! warning "The practical consequence"

    ```python
    # Meters — set_actor_location is a first-class, unit-converted RPC.
    tw.set_actor_location(actor="MyActor", location=Geometry.Vector(x=1.0, y=2.0, z=3.0))

    # Centimeters — RelativeLocation is a raw UProperty.
    tw.set_vector_property(actor="MyActor", component="Mesh",
                           property="RelativeLocation", x=100.0, y=200.0, z=300.0)
    ```

    Values you *read back* through `get_*_properties` are likewise Unreal-native and unconverted,
    as are values returned by [`call_function`](../plugins/tempo-world.md#calling-functions).

## Scale is unitless

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
