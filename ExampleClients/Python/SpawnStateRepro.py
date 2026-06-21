# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Repro / diagnostic for: "spawn succeeds and the actor shows up in GetAllActors,
# but it's missing from the WorldState (sim state) RPCs and command_vehicle reports
# 'no commandable pawn found'."
#
# This script spawns an actor and then probes every relevant RPC so we can see
# *which* lookup drops it. It distinguishes the two known mechanisms:
#
#   1) The "near" WorldState RPCs (GetCurrentActorStatesNear / ...NearPosition) skip
#      any actor that has no UMovementComponent / UMassTrafficVehicleComponent /
#      UMassAgentComponent unless include_static=True. A robot without one of those
#      is silently treated as "static" and excluded -- even though GetAllActors (no
#      filter) and GetCurrentActorState (exact-name, no filter) still return it.
#
#   2) A deferred spawn (deferred=True) that never had FinishSpawningActor called.
#      BeginPlay never runs, so the TempoMovementController never possesses the pawn
#      -> not commandable. Depending on the actor it can also fail the "near" filter.
#
# Usage:
#   python SpawnStateRepro.py --type BP_MyRobot
#   python SpawnStateRepro.py --type BP_MyRobot --deferred       # reproduce the deferred case
#   python SpawnStateRepro.py --type BP_MyRobot --x 0 --y 0 --z 1

import argparse
import asyncio

import grpc

import tempo_sim
import tempo_sim.tempo_world as tw
import tempo_sim.tempo_movement as tm
import tempo_sim.TempoCore.Geometry_pb2 as Geometry


def ok(label, detail=""):
    print(f"  [ OK ] {label}{(' -- ' + detail) if detail else ''}")


def fail(label, detail=""):
    print(f"  [FAIL] {label}{(' -- ' + detail) if detail else ''}")


async def actor_in_get_all_actors(name):
    resp = await tw.get_all_actors()
    names = [a.name for a in resp.actors]
    return name in names, names


async def try_get_current_actor_state(name):
    """Exact-name lookup (GetActorWithName). No static/hidden filtering -- only fails on a NAME mismatch."""
    try:
        state = await tw.get_current_actor_state(actor=name)
        return True, state, None
    except grpc.aio._call.AioRpcError as e:
        return False, None, f"{e.code().name}: {e.details()}"


def c_suffix_variant(name):
    """Reconstruct the raw FName form by re-inserting the Blueprint '_C' that the
    de-'_C' actor label drops: 'BP_Robot0' / 'BP_Robot_0' -> 'BP_Robot_C_0'.
    Used to confirm the regression is specifically the label/_C asymmetry."""
    import re
    m = re.match(r"^(.*?)_?(\d+)$", name)
    if m:
        return f"{m.group(1)}_C_{m.group(2)}"
    return f"{name}_C"


async def try_states_near_position(position, radius_m, include_static, include_hidden):
    resp = await tw.get_current_actor_states_near_position(
        position=position,
        search_radius_m=radius_m,
        include_static=include_static,
        include_hidden_actors=include_hidden,
    )
    return [s.name for s in resp.actor_states]


async def try_command(name):
    try:
        await tm.command_vehicle(vehicle=name, acceleration=0.0, steering=0.0)
        return True, None
    except grpc.aio._call.AioRpcError as e:
        return False, f"{e.code().name}: {e.details()}"


async def probe(name, position):
    """Run every relevant lookup against an actor named `name` and report."""
    print(f"\n  Probing actor '{name}' ...")

    present, names = await actor_in_get_all_actors(name)
    (ok if present else fail)("GetAllActors contains the actor",
                              "" if present else f"actors seen: {names}")

    found, _, err = await try_get_current_actor_state(name)
    if found:
        ok("GetCurrentActorState (exact name, no filter) found it")
    else:
        fail("GetCurrentActorState (exact name, no filter)", err)
        print("        ^ exact-name lookup failed => NAME-mismatch bug, not the static filter. "
              "Producers report UTempoCoreUtils::GetActorIdentifier (de-'_C' label), but the "
              "consumer GetActorWithName fell back to GetName() (with '_C') at query time.")
        # Prove it: the same actor should resolve under its raw '_C' FName form.
        variant = c_suffix_variant(name)
        v_found, _, _ = await try_get_current_actor_state(variant)
        if v_found:
            print(f"        ^ CONFIRMED: it DID resolve as '{variant}'. The de-'_C' identifier "
                  "the client was handed is not round-trippable -- exactly the commit 28f0ae0c "
                  "asymmetry (GetActorWithName not migrated to GetActorIdentifier).")

    near_default = await try_states_near_position(position, 50.0,
                                                  include_static=False, include_hidden=False)
    near_static = await try_states_near_position(position, 50.0,
                                                 include_static=True, include_hidden=False)
    near_all = await try_states_near_position(position, 50.0,
                                              include_static=True, include_hidden=True)

    in_default = name in near_default
    in_static = name in near_static
    in_all = name in near_all

    (ok if in_default else fail)("...NearPosition (include_static=False, default)")
    (ok if in_static else fail)("...NearPosition (include_static=True)")
    (ok if in_all else fail)("...NearPosition (include_static=True, include_hidden=True)")

    if (not in_default) and in_static:
        print("        ^ Excluded by the STATIC filter. The actor has no "
              "UMovementComponent / UMassTrafficVehicleComponent / UMassAgentComponent, "
              "so the near-queries treat it as static and drop it by default.")
    if (not in_static) and in_all:
        print("        ^ Excluded by the HIDDEN filter (actor IsHidden()).")

    pawns_resp = await tm.get_commandable_pawns()
    pawns = list(pawns_resp.pawns)
    in_pawns = name in pawns
    (ok if in_pawns else fail)("GetCommandablePawns lists it", f"pawns: {pawns}")

    commanded, cerr = await try_command(name)
    if commanded:
        ok("command_vehicle accepted")
    else:
        fail("command_vehicle", cerr)
        print("        ^ Not commandable: no ATempoMovementController is possessing this "
              "pawn. Either BeginPlay/possession hasn't happened (deferred spawn not "
              "finished) or the actor's AIControllerClass isn't a TempoMovementController.")


async def main():
    parser = argparse.ArgumentParser(description="Spawn/state repro for Tempo")
    parser.add_argument('--ip', default="0.0.0.0")
    parser.add_argument('--port', type=int, default=10001)
    parser.add_argument('--type', required=True, help="Actor class name to spawn (e.g. BP_MyRobot)")
    parser.add_argument('--deferred', action='store_true',
                        help="Spawn with deferred=True and probe BEFORE and AFTER FinishSpawningActor")
    parser.add_argument('--x', type=float, default=0.0)
    parser.add_argument('--y', type=float, default=0.0)
    parser.add_argument('--z', type=float, default=0.0)
    args = parser.parse_args()

    if args.ip != "0.0.0.0" or args.port != 10001:
        await tempo_sim.set_server(address=args.ip, port=args.port)

    transform = Geometry.Transform()
    transform.location.x = args.x
    transform.location.y = args.y
    transform.location.z = args.z
    position = Geometry.Vector(x=args.x, y=args.y, z=args.z)

    print("\n=== Spawn / Sim-State Repro ===")
    print(f"Spawning '{args.type}' at ({args.x}, {args.y}, {args.z}) deferred={args.deferred}")

    try:
        resp = await tw.spawn_actor(actor_type=args.type, deferred=args.deferred, transform=transform)
    except grpc.aio._call.AioRpcError as e:
        print(f"  SpawnActor failed: {e.code().name}: {e.details()}")
        return

    name = resp.name
    print(f"  SpawnActor returned name = '{name}'")

    if args.deferred:
        print("\n--- BEFORE FinishSpawningActor (BeginPlay has NOT run) ---")
        await probe(name, position)

        print("\n--- Calling FinishSpawningActor ---")
        try:
            await tw.finish_spawning_actor(actor=name)
            print("  FinishSpawningActor OK")
        except grpc.aio._call.AioRpcError as e:
            print(f"  FinishSpawningActor failed: {e.code().name}: {e.details()}")
            print("  ^ If this is NOT_FOUND, the spawn-returned name no longer resolves "
                  "via GetActorWithName -- a name-mismatch bug.")

        print("\n--- AFTER FinishSpawningActor ---")
        await probe(name, position)
    else:
        await probe(name, position)

    print("\nDone.\n")


if __name__ == "__main__":
    asyncio.run(main())
