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
TempoMovement can drive a Pawn along a predefined, timed path. The path is a `SplineTrajectory` Actor, which holds a real `USplineComponent` (editable in the level with the spline gizmo). Its `SpeedMode` selects how trajectory time maps onto the spline geometry:
- `ConstantSpeed`: traverse the spline at a fixed `Speed` (default 100 cm/s).
- `SplinePointVsTime`: a `TimeToInputKey` curve maps time to a spline input key (point index), so you can specify when the path reaches each point — and dwell, accelerate, or reverse.
- `DistanceVsTime`: a `TimeToDistance` curve maps time directly to distance along the spline (cm).
- `SpeedVsTime`: a `TimeToSpeed` curve gives speed along the spline (cm/s) over time; distance is its integral.

A `SplineTrajectory` is just shared path data. To make a Pawn follow one, add a `TrajectoryFollowingComponent` to the Pawn and point its `Trajectory` at a placed trajectory. On `BeginPlay` the component spawns a `TrajectoryFollowingController` that possesses the Pawn and drives it. Because the trajectory is referenced rather than owned, several Pawns can each carry their own component pointing at the same trajectory, each with its own `Config`:
- `bTeleport` (default true): set the Pawn's pose to the target each tick (exact). If false, the controller steers toward the target and lets the Pawn's movement component respond — via `AddMovementInput` (`PositionGain`/`InputScale`) for most Pawns, or throttle/steer/brake for wheeled vehicles (Chaos or kinematic), which can't strafe (`PositionGain`/`YawRateGain`).
- `EndBehavior` (default `Clamp`): what to do at the end of the trajectory — `Clamp` (hold the final pose), `Loop` (wrap to the start; a steering follower drives back), or `Reset` (teleport the Pawn — even a steering one — back to the start and repeat).

Trajectory time accumulates from the simulation frame delta starting when following begins, and holds steady while the sim is paused or between fixed steps. To delay or stagger a Pawn, add its component when it should begin.

### Configuring a trajectory at runtime
A trajectory's spline and speed model can be set over the `configure_trajectory` RPC. Each point gives a world-frame location and an un-normalized tangent (both in meters); at least two are required. Timing is set separately via exactly one of `constant_speed` (m/s) or a `spline_point_vs_time` / `distance_vs_time` / `speed_vs_time` curve (each a list of `{time, value}` keys, where `value` is a spline input key, meters, or m/s respectively). For example:
```
import tempo_sim.tempo_movement as tm
import tempo_sim.TempoMovement.MovementControlService_pb2 as mcs

def point(location, tangent):
    p = mcs.SplineTrajectoryPoint()
    p.location.x, p.location.y, p.location.z = location
    p.tangent.x, p.tangent.y, p.tangent.z = tangent
    return p

# Constant speed (2 m/s) along the spline:
tm.configure_trajectory(trajectory="MyTrajectory", constant_speed=2.0, points=[
    point(location=(0.0, 0.0, 0.0), tangent=(1.0, 0.0, 0.0)),
    point(location=(10.0, 0.0, 0.0), tangent=(1.0, 0.0, 0.0)),
])

# Or specify when the path reaches each point (SplinePointVsTime): value = point index.
curve = mcs.TrajectoryCurve()
for t, idx in [(0.0, 0), (5.0, 1)]:
    k = curve.keys.add(); k.time, k.value = t, idx
tm.configure_trajectory(trajectory="MyTrajectory", spline_point_vs_time=curve, points=[
    point(location=(0.0, 0.0, 0.0), tangent=(1.0, 0.0, 0.0)),
    point(location=(10.0, 0.0, 0.0), tangent=(1.0, 0.0, 0.0)),
])
```
