# TempoMovement
TempoMovement includes controllers and dynamics models for simulating the movement of Actors.

## Vehicle Control
TempoMovement includes movement models for vehicles (a kinematic bicycle model, a kinematic unicycle model, and a Chaos wheeled vehicle) and a controller for vehicles. You can control a simulated vehicle with the `command_vehicle` RPC. For example:
```
import tempo_sim.tempo_movement as tm

commandable_vehicles_response = tm.get_commandable_vehicles()
tm.command_vehicle(vehicle="MyVehicle", acceleration=0.5, steering=0.0) # Acceleration and steering are normalized from -1.0 to 1.0.
```

### Rotation center
The kinematic models (`KinematicBicycleModelMovementComponent`, `KinematicUnicycleModelMovementComponent`) turn about `RotationCenter`, an owner-local XY point on the movement component. It defaults to zero, which turns about the owner's origin. Set it when the owner's origin is not where the vehicle should pivot — for instance a mesh whose origin sits at the rear bumper. `RotationCenter` is also the point whose velocity the motion model describes, so it is the point that tracks a commanded speed exactly. It is an unscaled owner-local offset (the owner's scale applies on top of it), and can be set at runtime with `set_vector2d_property`.

For the bicycle model this is independent of `AxleRatio`. `AxleRatio` places the model's reference point along the wheelbase (`0` = rear axle, `1` = front axle), which sets the slip angle; `RotationCenter` says where that same point sits on the owner. A vehicle with a 300 cm wheelbase whose rear axle is 50 cm ahead of its origin uses `AxleRatio = 0`, `RotationCenter = (50, 0)` to turn about its rear axle, or `AxleRatio = 0.5`, `RotationCenter = (200, 0)` to turn about the middle of its wheelbase.

Because yaw is about the world Z axis, only the horizontal offset matters, which is why `RotationCenter` is 2D. `GroundSnapComponent` takes the same kind of offset as `ExtentsCenter`, so an off-center origin usually wants both set consistently.

## Pawn Movement
TempoMovement also supports controlling pawns, using Unreal's navigation system. You can control a simulated Pawn (like a humanoid Character). For example:
```
import tempo_sim.tempo_movement as tm
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

location = Geometry.vector()
location.x = -10
tm.pawn_move_to_location(pawn="MyPawn", location=location, relative=True) # relative defaults to False (meaning relative to world, not the Pawn's current location)
```

## Trajectory Following
TempoMovement can drive a Pawn along a predefined, timed path. The path's geometry is a `SplineActor` — a bare Actor whose root is a real `USplineComponent` (editable in the level with the spline gizmo), the way `AStaticMeshActor` is a bare Actor whose root is a `UStaticMeshComponent`. A `SplineActor` is pure geometry and carries no timing of its own.

To make a Pawn follow a spline, add a `TrajectoryFollowingComponent` to the Pawn and point its `Spline` at a placed `SplineActor`. On `BeginPlay` the component spawns a `TrajectoryFollowingController` that possesses the Pawn and drives it. Because the spline is referenced rather than owned, several Pawns can each carry their own component pointing at the same `SplineActor`, each with its own `Config`. The `Config` owns both the speed model (how trajectory time maps onto the spline) and how the Pawn is driven to the target:
- `SpeedMode` selects how trajectory time maps onto the spline geometry:
  - `ConstantSpeed`: traverse the spline at a fixed `Speed` (default 100 cm/s).
  - `SplinePointVsTime`: a `TimeToInputKey` curve maps time to a spline input key (point index), so you can specify when the path reaches each point — and dwell, accelerate, or reverse.
  - `DistanceVsTime`: a `TimeToDistance` curve maps time directly to distance along the spline (cm).
  - `SpeedVsTime`: a `TimeToSpeed` curve gives speed along the spline (cm/s) over time; distance is its integral.
- `bTeleport` (default true): set the Pawn's pose to the target each tick (exact). If false, the controller steers toward the target and lets the Pawn's movement component respond — via `AddMovementInput` (`PositionGain`/`InputScale`) for most Pawns, or throttle/steer/brake for wheeled vehicles (Chaos or kinematic), which can't strafe (`PositionGain`/`YawRateGain`).
- `EndBehavior` (default `Clamp`): what to do at the end of the trajectory — `Clamp` (hold the final pose), `Loop` (wrap to the start; a steering follower drives back), or `Reset` (teleport the Pawn — even a steering one — back to the start and repeat).

Because the speed model lives on the follower rather than the spline, two Pawns can share one `SplineActor` while traversing it at different speeds.

Trajectory time accumulates from the simulation frame delta starting when following begins, and holds steady while the sim is paused or between fixed steps. To delay or stagger a Pawn, add its component when it should begin.

### Configuring trajectory following at runtime
Spline geometry and follower timing are configured by two separate RPCs.

Set a `SplineActor`'s geometry with `set_spline_points`. Each point gives a world-frame location and an un-normalized tangent (both in meters); at least two are required:
```
import tempo_sim.tempo_movement as tm
import tempo_sim.TempoMovement.MovementControlService_pb2 as mcs

def point(location, tangent):
    p = mcs.SplinePoint()
    p.location.x, p.location.y, p.location.z = location
    p.tangent.x, p.tangent.y, p.tangent.z = tangent
    return p

tm.set_spline_points(spline="MySpline", points=[
    point(location=(0.0, 0.0, 0.0), tangent=(1.0, 0.0, 0.0)),
    point(location=(10.0, 0.0, 0.0), tangent=(1.0, 0.0, 0.0)),
])
```

Then make a Pawn follow that spline with `configure_trajectory_following`. The Pawn must carry a `TrajectoryFollowingComponent`. Timing is set via exactly one of `constant_speed` (m/s) or a `spline_point_vs_time` / `distance_vs_time` / `speed_vs_time` curve (each a list of `{time, value}` keys, where `value` is a spline input key, meters, or m/s respectively). Optionally override `follow_mode` and `end_behavior`; unspecified, they keep the component's authored values:
```
# Constant speed (2 m/s) along the spline:
tm.configure_trajectory_following(pawn="MyPawn", spline="MySpline", constant_speed=2.0)

# Or specify when the path reaches each point (SplinePointVsTime): value = point index,
# and drive toward the path instead of teleporting:
curve = mcs.TrajectoryCurve()
for t, idx in [(0.0, 0), (5.0, 1)]:
    k = curve.keys.add(); k.time, k.value = t, idx
tm.configure_trajectory_following(
    pawn="MyPawn", spline="MySpline", spline_point_vs_time=curve,
    follow_mode=mcs.TRAJECTORY_FOLLOW_MODE_DRIVE,
    end_behavior=mcs.TRAJECTORY_END_BEHAVIOR_LOOP)
```
