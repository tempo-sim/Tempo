# TempoMovement

TempoMovement provides controllers and dynamics models for moving actors: vehicles, pawns, and
anything that should follow a timed path.

## Vehicle control

TempoMovement includes movement models for vehicles — a kinematic bicycle model, a kinematic
unicycle model, and a Chaos wheeled vehicle — plus a controller for them.

```python
import tempo_sim.tempo_movement as tm

tm.get_commandable_vehicles()
# Acceleration and steering are normalized from -1.0 to 1.0.
tm.command_vehicle(vehicle="MyVehicle", acceleration=0.5, steering=0.0)
```

TempoSample's street sweeper is a commandable vehicle, so this works there without any setup.

### Velocity and acceleration commands

Beyond the normalized throttle/steer form, pawns can be commanded in physical units — closed-loop,
in the pawn's own frame:

```python
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

twist = Geometry.Twist()
twist.linear.x = 2.0        # m/s forward
twist.angular.z = 0.3       # rad/s yaw
tm.command_velocity(pawn="MyPawn", twist=twist)

accel = Geometry.Accel()    # same frame and conventions, in m/s^2 and rad/s^2
accel.linear.x = 0.5
tm.command_acceleration(pawn="MyPawn", accel=accel)
```

### Rotation center

The kinematic models (`KinematicBicycleModelMovementComponent`,
`KinematicUnicycleModelMovementComponent`) turn about `RotationCenter`, an owner-local XY point on
the movement component. It defaults to zero, which turns about the owner's origin.

Set it when the owner's origin is not where the vehicle should pivot — a mesh whose origin sits at
the rear bumper, for instance. `RotationCenter` is also the point whose velocity the motion model
describes, so it is the point that tracks a commanded speed exactly. It is an *unscaled*
owner-local offset (the owner's scale applies on top of it), and can be set at runtime with
`set_vector2d_property`.

For the bicycle model this is independent of `AxleRatio`. `AxleRatio` places the model's reference
point along the wheelbase (`0` = rear axle, `1` = front axle), which sets the slip angle;
`RotationCenter` says where that same point sits on the owner.

!!! example "A 300 cm wheelbase whose rear axle is 50 cm ahead of its origin"

    | Goal | `AxleRatio` | `RotationCenter` |
    |---|---|---|
    | Turn about the rear axle | `0` | `(50, 0)` |
    | Turn about the middle of the wheelbase | `0.5` | `(200, 0)` |

Because yaw is about the world Z axis, only the horizontal offset matters — which is why
`RotationCenter` is 2D. `GroundSnapComponent` takes the same kind of offset as `ExtentsCenter`, so
an off-center origin usually wants both set consistently.

## Pawn movement

TempoMovement also drives pawns through Unreal's navigation system:

```python
import tempo_sim.tempo_movement as tm
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

tm.get_navigable_pawns()

location = Geometry.Vector()
location.x = -10
# relative defaults to False, meaning relative to the world rather than the pawn
tm.pawn_move_to_location(pawn="MyPawn", location=location, relative=True)
```

The response carries a `MoveToResult` — `MTR_SUCCESS`, `MTR_BLOCKED`, and so on — so a client can
tell a completed move from a failed one.

If you have changed the world enough to invalidate the navmesh — spawned buildings, moved
obstacles — `rebuild_navigation()` regenerates it.

## Trajectory following

TempoMovement can drive a pawn along a predefined, timed path.

The path's geometry is a **`SplineActor`** — a bare actor whose root is a real `USplineComponent`,
editable in the level with the spline gizmo, the way `AStaticMeshActor` is a bare actor whose root
is a `UStaticMeshComponent`. A `SplineActor` is pure geometry and carries no timing of its own.

To make a pawn follow one, add a **`TrajectoryFollowingComponent`** to the pawn and point its
`Spline` at a placed `SplineActor`. On `BeginPlay` the component spawns a
`TrajectoryFollowingController` that possesses the pawn and drives it.

Because the spline is *referenced* rather than owned, several pawns can each carry their own
component pointing at the same `SplineActor`, each with its own `Config` — so two pawns can share
one path while traversing it at different speeds.

### Config

The `Config` owns both the speed model (how trajectory time maps onto the spline) and how the pawn
is driven to the target.

`SpeedMode` — how trajectory time maps onto the spline geometry:

| Mode | Meaning |
|---|---|
| `ConstantSpeed` | Traverse the spline at a fixed `Speed` (default 100 cm/s). |
| `SplinePointVsTime` | A `TimeToInputKey` curve maps time to a spline input key (point index), so you can specify when the path reaches each point — and dwell, accelerate, or reverse. |
| `DistanceVsTime` | A `TimeToDistance` curve maps time directly to distance along the spline (cm). |
| `SpeedVsTime` | A `TimeToSpeed` curve gives speed along the spline (cm/s) over time; distance is its integral. |

`bTeleport` (default `true`)

:   Set the pawn's pose to the target each tick — exact. If false, the controller steers *toward*
    the target and lets the pawn's movement component respond: via `AddMovementInput`
    (`PositionGain` / `InputScale`) for most pawns, or throttle/steer/brake for wheeled vehicles
    (Chaos or kinematic), which can't strafe (`PositionGain` / `YawRateGain`).

`EndBehavior` (default `Clamp`)

:   What to do when the trajectory reaches its end:

    | Behavior | Effect |
    |---|---|
    | `Clamp` | Hold the final pose. |
    | `Loop` | Wrap to the start and keep going; a steering follower drives back. |
    | `Reset` | Teleport the pawn — even a steering one — back to the start and repeat. |
    | `Destroy` | Destroy the pawn. |

    "The end" means when the *trajectory* reaches its end, not the pawn. A steering follower lags
    its target, so under `Destroy` it is destroyed while still short of the spline's final point.

Trajectory time accumulates from the *simulation* frame delta starting when following begins, and
holds steady while the sim is paused or between fixed steps. To delay or stagger a pawn, add its
component when it should begin.

### Configuring trajectory following at runtime

Spline geometry and follower timing are configured by two separate RPCs.

**Set a `SplineActor`'s geometry** with `set_spline_points`. Each point gives a world-frame
location and an un-normalized tangent, both in meters; at least two are required.

```python
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

**Then make a pawn follow it** with `configure_trajectory_following`. The pawn must carry a
`TrajectoryFollowingComponent`. Timing is set via exactly one of `constant_speed` (m/s) or a
`spline_point_vs_time` / `distance_vs_time` / `speed_vs_time` curve — each a list of
`{time, value}` keys, where `value` is a spline input key, meters, or m/s respectively.

`follow_mode` and `end_behavior` are optional; unspecified, they keep the component's authored
values.

```python
# Constant speed (2 m/s) along the spline:
tm.configure_trajectory_following(pawn="MyPawn", spline="MySpline", constant_speed=2.0)

# Or specify when the path reaches each point (SplinePointVsTime): value = point index,
# and drive toward the path instead of teleporting:
curve = mcs.TrajectoryCurve()
for t, idx in [(0.0, 0), (5.0, 1)]:
    k = curve.keys.add()
    k.time, k.value = t, idx

tm.configure_trajectory_following(
    pawn="MyPawn", spline="MySpline", spline_point_vs_time=curve,
    follow_mode=mcs.TRAJECTORY_FOLLOW_MODE_DRIVE,
    end_behavior=mcs.TRAJECTORY_END_BEHAVIOR_LOOP)
```

### Re-pacing and end events

`set_trajectory_speed` re-paces a pawn already following a trajectory, without reconfiguring it:

```python
tm.set_trajectory_speed(pawn="MyPawn", speed=0.0)    # hold in place, indefinitely
tm.set_trajectory_speed(pawn="MyPawn", speed=2.0)    # resume from the same point
tm.set_trajectory_speed(pawn="MyPawn", speed=-1.0)   # drive back along the spline
```

`0` holds the pawn where it is, indefinitely and without ending the trajectory; following resumes
from the same point when a speed is set again. A negative speed drives back along the spline — a
steering follower reverses, keeping its heading rather than turning around — and stops at the
spline's start without ending the trajectory.

`stream_trajectory_end_events` reports the end of each trajectory a pawn is given:

```python
for event in tm.stream_trajectory_end_events(pawn="MyPawn"):
    ...
```

The stream is per-pawn and survives reconfiguration, reporting the end of each trajectory the pawn
is given in turn. **It carries only the ends that happen while it is open and keeps no history**,
so subscribe *before* configuring the trajectory whose end you care about. A destroyed pawn ends
the stream with `ABORTED` — including under the `Destroy` end behavior, whose own event is sent
first.

!!! note "Why a dedicated RPC for spline points"

    `USplineComponent`'s points are not reachable through TempoWorld's generic property API, so
    spline geometry gets a first-class RPC rather than a `set_*_property` call.

## API reference

[:octicons-arrow-right-24: `TempoMovement` RPCs](../reference/api/tempo-movement.md)
