# TempoROSBridge

TempoROSBridge adapts Tempo's existing gRPC-shaped services and sensor data onto ROS 2 topics and
services, so a ROS graph can drive and observe the simulation without speaking gRPC.

It sits on top of [TempoROS](tempo-ros.md), which does the actual `rclcpp` integration, and on top
of the Tempo plugins whose services it re-exposes.

!!! info "Both plugins are optional"

    Tempo's primary interface is gRPC and needs no ROS installation. Enable TempoROS and
    TempoROSBridge only if you want a ROS graph in the loop. TempoSample enables both by default;
    if you are not using ROS, disable them in the `.uproject` and remove `TempoROSCopyHandler`
    from `Config/DefaultGame.ini`.

## How it works

The bridge is a set of Unreal **world subsystems**, one per Tempo plugin it bridges. On
`OnWorldBeginPlay` each creates a `UTempoROSNode` and binds Tempo's existing service handlers to
ROS services — the same handler code serves both gRPC and ROS, so the two interfaces cannot drift.

```cpp
ROSNode = UTempoROSNode::Create("TempoTime", this);
BindServiceToROS<FTempoAdvanceStepsService>(ROSNode, "AdvanceSteps",
                                            this, &UTempoTimeROSBridgeSubsystem::AdvanceSteps);
```

Request and response types are custom ROS IDL definitions living in each bridge module's
`msg/` and `srv/` folders, generated into ROS packages at build time (`tempo_core_ros_bridge`,
`tempo_movement_ros_bridge`, and so on). See
[Custom message and service types](tempo-ros.md) on the TempoROS site for how that generation
works.

!!! note "Bridged services must answer immediately"

    `BindServiceToROS` requires the underlying Tempo handler to invoke its response continuation
    synchronously — a ROS service call has no deferred-response form here. Handlers that complete
    later (a deferred level load, a sensor waiting on GPU readback) are not bridged this way.

## What's on the graph

Node names are shown below; topics are published under `<NodeName>/…`, while services keep the
bare names given.

### TempoTime

Service names: `/AdvanceSteps`, `/SetSimStepsPerSecond`, `/SetTimeMode`, `/Play`, `/Pause`,
`/Step`.

| Service | Request | Response |
|---|---|---|
| `AdvanceSteps` | `int32 steps` | — |
| `SetSimStepsPerSecond` | `int32 sim_steps_per_second` | — |
| `SetTimeMode` | `string time_mode` | — |
| `Play` / `Pause` / `Step` | — (`std_srvs/srv/Empty`) | — |

TempoROS also runs a **clock server** that publishes simulation time on `/clock` every frame, so
ROS nodes with `use_sim_time` follow Tempo's time. Disable it (`Publish Clock` in the TempoROS
settings) if something else in your graph is the time authority.

### TempoMovement

| Service | Request | Response |
|---|---|---|
| `GetCommandablePawns` | — | `string[] pawn_names` |
| `CommandVehicle` | `string pawn_name`, `float32 acceleration`, `float32 steering` | — |
| `CommandVelocity` | `string pawn_name`, `geometry_msgs/Twist twist` | — |
| `CommandAcceleration` | `string pawn_name`, `geometry_msgs/Accel accel` | — |

`CommandVelocity` and `CommandAcceleration` take standard `geometry_msgs` types, so an existing
ROS controller can drive a Tempo pawn without a custom message.

### TempoGeographic

| Service | Request | Response |
|---|---|---|
| `SetGeographicReference` | `float64 latitude`, `longitude`, `altitude`, `roll`, `pitch`, `yaw` | — |
| `SetTimeOfDay` | `int32 hour`, `minute`, `second` | — |
| `SetDayCycleRate` | `float32 rate` | — |
| `GetDateTime` | — | `int32 day`, `month`, `year`, `hour`, `minute`, `second` |

`SetGeographicReference` takes the WGS84 coordinate of the Unreal world origin, plus its
orientation relative to the local **North-West-Up** frame in radians, right-handed — matching
[TempoGeographic](tempo-geographic.md).

### TempoSensors

| Service | Request | Response |
|---|---|---|
| `GetAvailableSensors` | — | `SensorDescriptor[] available_sensors` |

`SensorDescriptor` carries `string owner`, `string name`, `float32 rate`, and
`string[] measurement_types`.

Camera measurements are published as topics, created and torn down automatically as sensors appear
and disappear:

| Topic | Message |
|---|---|
| `TempoSensors/image/color/<owner>/<sensor>` | color image |
| `TempoSensors/image/depth/<owner>/<sensor>` | depth image |
| `TempoSensors/image/label/<owner>/<sensor>` | label image |
| `<base topic>/camera_info` | `FTempoCameraInfo`, from the camera's intrinsics |

Owner and sensor names are lower-cased in the topic. Publishers are only fed while something is
subscribed, so an idle topic costs nothing.

Because these are `sensor_msgs`-shaped image messages, TempoROS's automatic
[image transport](tempo-ros.md) integration applies — each image topic also gains the compressed
transports ROS's image-transport plugins provide.

!!! note "Lidar scans are not bridged yet"

    Lidar has a topic name reserved (`TempoSensors/scan/<owner>/<sensor>`), but no publisher is
    created for it today. Use the [gRPC API](../reference/api/tempo-sensors.md) for lidar, or
    publish it yourself from a `UTempoROSNode`.

### Transforms

For every active sensor the bridge publishes two static transforms via tf2:

- `<owner>/<sensor>` relative to `<owner>` — the sensor's pose on its actor.
- `<owner>/<sensor>/optical` relative to `<owner>/<sensor>` — a fixed rotation
  (`FRotator(0, 90, -90)`) giving the optical convention, Z pointing out of the camera, which is
  what most ROS vision nodes expect.

The root of the transform tree is TempoROS's configured `Fixed Frame Name` (default `map`).

## Trying it out

With TempoROS's bundled environment:

```bash
source Plugins/Tempo/TempoROS/Scripts/ROSEnv.sh

ros2 node list
ros2 service list
ros2 topic list
ros2 topic echo /TempoSensors/image/color/bp_sensorrig/tempocamera --once

ros2 service call /SetTimeMode tempo_core_ros_bridge/srv/SetTimeMode "{time_mode: 'FixedStep'}"
ros2 service call /Step std_srvs/srv/Empty
```

## Extending the bridge

The bridge modules are a readable template for exposing your own Tempo services on ROS. Each one
is: a module with `msg/` and `srv/` IDL folders, a converter header, and a world subsystem that
creates a node and calls `BindServiceToROS` once per service.

If your project defines its own gRPC services (see
[Adding your own services](../guides/custom-services.md)), the same pattern bridges them.
