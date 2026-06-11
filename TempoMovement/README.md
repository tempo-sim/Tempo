# TempoMovement
TempoMovement includes controllers and dynamics models for simulating the movement of Actors.

## Vehicle Control
TempoMovement includes movement models for vehicles (currently only a simple kinematic bicycle model) and a controller for vehicles. You can control a simulated vehicle with the `command_vehicle` RPC. For example:
```
import tempo_sim.tempo_movement as tm

commandable_vehicles_response = tm.get_commandable_vehicles()
tm.command_vehicle(vehicle="MyVehicle", acceleration=0.5, steering=0.0) # Acceleration and steering are normalized from -1.0 to 1.0.
```

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
TempoMovement can drive a Pawn along a predefined, timed path. The path is described by a `SplineTrajectory` Actor, which holds a real `USplineComponent` (editable in the level with the spline gizmo). There are two concrete kinds:
- `ConstantSpeedSplineTrajectory` traverses the spline at a fixed `Speed` (default 100 cm/s).
- `CurveSplineTrajectory` follows a time-to-spline-input-key curve, so you can specify when the path reaches each point.

A `SplineTrajectory` is just shared path data. To make a Pawn follow one, add a `TrajectoryFollowingComponent` to the Pawn and point its `Trajectory` at a placed trajectory. On `BeginPlay` the component spawns a `TrajectoryFollowingController` that possesses the Pawn and drives it. Because the trajectory is referenced rather than owned, several Pawns can each carry their own component pointing at the same trajectory. The component's `Config` controls how it follows:
- `bTeleport` (default true): set the Pawn's pose to the trajectory's target each tick (exact). If false, the controller steers toward the target with `AddMovementInput`, using `PositionGain` and `InputScale`, and lets the Pawn's movement component respond.

Trajectory time starts when the controller spawns, so to delay or stagger a Pawn, spawn it (or add its component) when it should begin.

### Configuring a trajectory at runtime
A trajectory's spline (and, for a `CurveSplineTrajectory`, its time curve) can be set over the `configure_trajectory` RPC. Each point gives a world-frame location and an un-normalized tangent (both in meters), plus the time (seconds, relative to the start) at which the path reaches it. At least two points are required. The `time` field is used by `CurveSplineTrajectory` and ignored by `ConstantSpeedSplineTrajectory`. For example:
```
import tempo_sim.tempo_movement as tm
import tempo_sim.TempoMovement.MovementControlService_pb2 as mcs

def point(location, tangent, time):
    p = mcs.SplineTrajectoryPoint()
    p.location.x, p.location.y, p.location.z = location
    p.tangent.x, p.tangent.y, p.tangent.z = tangent
    p.time = time
    return p

tm.configure_trajectory(trajectory="MyTrajectory", points=[
    point(location=(0.0, 0.0, 0.0), tangent=(1.0, 0.0, 0.0), time=0.0),
    point(location=(10.0, 0.0, 0.0), tangent=(1.0, 0.0, 0.0), time=5.0),
])
```
