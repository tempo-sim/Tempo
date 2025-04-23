# TempoMovement
TempoMovement includes controllers and dynamics models for simulating the movement of Actors.

## Vehicle Control
TempoMovement includes movement models for vehicles (currently only a simple kinematic bicycle model) and a controller for vehicles. You can control a simulated vehicle with the `command_vehicle` RPC. For example:
```
import tempo.tempo_movement as tm

commandable_vehicles_response = tm.get_commandable_vehicles()
tm.command_vehicle(vehicle_name="MyVehicle", acceleration=0.5, steering=0.0) # Acceleration and steering are normalized from -1.0 to 1.0.
```

## Pawn Movement
TempoMovement also supports controlling pawns, using Unreal's navigation system. You can control a simulated Pawn (like a humanoid Character). For example:
```
import tempo.tempo_movement as tm
import TempoScripting.Geometry_pb2 as Geometry

location = Geometry.vector()
location.x = -10
tm.pawn_move_to_location(name="MyPawn", location=location, relative=True) # relative defaults to False (meaning relative to world, not the Pawn's current location)
```
