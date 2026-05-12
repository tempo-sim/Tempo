# Copyright Tempo Simulation, LLC. All Rights Reserved

import argparse
import asyncio
import grpc

import tempo
import tempo.tempo_movement as tm
import TempoCore.Geometry_pb2 as Geometry
import TempoMovement.MovementControlService_pb2 as Movement

from prompt_toolkit import PromptSession
from prompt_toolkit.completion import FuzzyWordCompleter
from prompt_toolkit.formatted_text import HTML


RESTART_SENTINEL = "__RESTART__"


# ---------------------------------------------------------------------------
# Fuzzy select helper
# ---------------------------------------------------------------------------

async def fuzzy_select(items, prompt_text, allow_restart=True):
    """Interactive fuzzy-filterable selection.

    items: list of (value, display_string) tuples
    Returns the selected value or RESTART_SENTINEL.
    """
    extra = []
    if allow_restart:
        extra.append((RESTART_SENTINEL, "< Restart >"))

    all_items = list(items) + extra

    if not all_items:
        print("  No items available.")
        return RESTART_SENTINEL

    display_to_value = {}
    displays = []
    for value, display in all_items:
        display_to_value[display] = value
        displays.append(display)

    completer = FuzzyWordCompleter(displays)
    session = PromptSession()

    while True:
        print()
        for idx, display in enumerate(displays):
            print(f"  [{idx}] {display}")
        print()

        try:
            user_input = await session.prompt_async(
                HTML(f"<b>{prompt_text}</b> (type to filter, # to select, Enter for [0]): "),
                completer=completer,
            )
        except (EOFError, KeyboardInterrupt):
            return RESTART_SENTINEL

        user_input = user_input.strip()

        if user_input == "":
            return all_items[0][0]

        try:
            idx = int(user_input)
            if 0 <= idx < len(all_items):
                return all_items[idx][0]
            else:
                print(f"  Index out of range (0-{len(all_items) - 1})")
                continue
        except ValueError:
            pass

        if user_input in display_to_value:
            return display_to_value[user_input]

        matches = [(v, d) for v, d in all_items if user_input.lower() in d.lower()]
        if len(matches) == 1:
            return matches[0][0]
        elif len(matches) > 1:
            all_items = matches
            display_to_value = {d: v for v, d in all_items}
            displays = [d for _, d in all_items]
            completer = FuzzyWordCompleter(displays)
            continue

        print(f"  No match for '{user_input}'. Try again.")


async def text_input(prompt_text, default=""):
    session = PromptSession()
    suffix = f" [{default}]" if default else ""
    try:
        result = await session.prompt_async(
            HTML(f"<b>{prompt_text}</b>{suffix}: "),
        )
    except (EOFError, KeyboardInterrupt):
        return default
    return result.strip() if result.strip() else default


# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------

def parse_floats(text, count, label):
    parts = text.split()
    if len(parts) != count:
        raise ValueError(f"Expected {count} values: {label}")
    return [float(p) for p in parts]


def move_to_result_string(result):
    return {
        Movement.MTR_UNKNOWN: "UNKNOWN",
        Movement.MTR_SUCCESS: "SUCCESS",
        Movement.MTR_BLOCKED: "BLOCKED",
        Movement.MTR_OFF_PATH: "OFF_PATH",
        Movement.MTR_ABORTED: "ABORTED",
        Movement.MTR_INVALID: "INVALID",
    }.get(result, f"({result})")


# ---------------------------------------------------------------------------
# Pawn fetchers
# ---------------------------------------------------------------------------

async def fetch_commandable_pawns():
    try:
        response = await tm.get_commandable_pawns()
        return list(response.pawns)
    except grpc.aio._call.AioRpcError as e:
        print(f"\n  Error fetching commandable pawns: {e.details()}")
        return []


async def fetch_navigable_pawns():
    try:
        response = await tm.get_navigable_pawns()
        return list(response.pawns)
    except grpc.aio._call.AioRpcError as e:
        print(f"\n  Error fetching navigable pawns: {e.details()}")
        return []


async def pick_pawn(prompt, pawns):
    if not pawns:
        print("  No pawns available.")
        return None
    items = [(p, p) for p in pawns]
    choice = await fuzzy_select(items, prompt)
    if choice == RESTART_SENTINEL:
        return None
    return choice


# ---------------------------------------------------------------------------
# Flows
# ---------------------------------------------------------------------------

async def flow_list_commandable():
    print("\n--- Commandable Pawns ---")
    pawns = await fetch_commandable_pawns()
    if not pawns:
        print("  (none)")
        return
    for p in pawns:
        print(f"  {p}")


async def flow_list_navigable():
    print("\n--- Navigable Pawns ---")
    pawns = await fetch_navigable_pawns()
    if not pawns:
        print("  (none)")
        return
    for p in pawns:
        print(f"  {p}")


async def flow_command_vehicle():
    print("\n--- Command Vehicle (Normalized Driving) ---")
    pawns = await fetch_commandable_pawns()
    pawn = await pick_pawn("Which vehicle?", pawns)
    if pawn is None:
        return

    accel_text = await text_input("Acceleration [-1, 1] (throttle/brake)", default="0.0")
    steering_text = await text_input("Steering [-1, 1] (left/right)", default="0.0")
    try:
        acceleration = float(accel_text)
        steering = float(steering_text)
    except ValueError as e:
        print(f"  {e}")
        return

    try:
        await tm.command_vehicle(vehicle=pawn, acceleration=acceleration, steering=steering)
        print(f"\n  Sent NormalizedDrivingCommand to {pawn}: accel={acceleration:.3f}, steer={steering:.3f}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error commanding vehicle: {e.details()}")


async def flow_command_velocity():
    print("\n--- Command Velocity (body frame: linear m/s, angular rad/s) ---")
    pawns = await fetch_commandable_pawns()
    pawn = await pick_pawn("Which pawn?", pawns)
    if pawn is None:
        return

    text = await text_input(
        "Twist (linear X Y Z, angular X Y Z; X forward, Y right, Z up)",
        default="0 0 0 0 0 0",
    )
    try:
        v = parse_floats(text, 6, "linear X Y Z angular X Y Z")
    except ValueError as e:
        print(f"  {e}")
        return

    twist = Geometry.Twist()
    twist.linear.x, twist.linear.y, twist.linear.z = v[0], v[1], v[2]
    twist.angular.x, twist.angular.y, twist.angular.z = v[3], v[4], v[5]

    try:
        await tm.command_velocity(pawn=pawn, twist=twist)
        print(f"\n  Sent VelocityCommand to {pawn}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error commanding velocity: {e.details()}")


async def flow_command_acceleration():
    print("\n--- Command Acceleration (body frame: linear m/s^2, angular rad/s^2) ---")
    pawns = await fetch_commandable_pawns()
    pawn = await pick_pawn("Which pawn?", pawns)
    if pawn is None:
        return

    text = await text_input(
        "Accel (linear X Y Z, angular X Y Z; X forward, Y right, Z up)",
        default="0 0 0 0 0 0",
    )
    try:
        v = parse_floats(text, 6, "linear X Y Z angular X Y Z")
    except ValueError as e:
        print(f"  {e}")
        return

    accel = Geometry.Accel()
    accel.linear.x, accel.linear.y, accel.linear.z = v[0], v[1], v[2]
    accel.angular.x, accel.angular.y, accel.angular.z = v[3], v[4], v[5]

    try:
        await tm.command_acceleration(pawn=pawn, accel=accel)
        print(f"\n  Sent AccelerationCommand to {pawn}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error commanding acceleration: {e.details()}")


async def flow_move_to_location():
    print("\n--- Pawn Move To Location ---")
    pawns = await fetch_navigable_pawns()
    pawn = await pick_pawn("Which pawn?", pawns)
    if pawn is None:
        return

    mode_items = [
        ("absolute", "Absolute (world frame)"),
        ("relative", "Relative (offset from pawn's current location)"),
    ]
    mode = await fuzzy_select(mode_items, "Coordinate mode?")
    if mode == RESTART_SENTINEL:
        return
    relative = (mode == "relative")

    text = await text_input(
        "Target X Y Z in meters, separated by spaces (e.g. 5 0 0)",
        default="0 0 0",
    )
    try:
        xyz = parse_floats(text, 3, "X Y Z")
    except ValueError as e:
        print(f"  {e}")
        return

    location = Geometry.Vector(x=xyz[0], y=xyz[1], z=xyz[2])

    print(f"\n  Moving {pawn} to ({xyz[0]:.2f}, {xyz[1]:.2f}, {xyz[2]:.2f})"
          f" ({'relative' if relative else 'absolute'}). Waiting for navigation to finish...")
    try:
        response = await tm.pawn_move_to_location(pawn=pawn, location=location, relative=relative)
        print(f"  Result: {move_to_result_string(response.result)}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error during move-to-location: {e.details()}")


async def flow_rebuild_navigation():
    print("\n--- Rebuild Navigation ---")
    try:
        await tm.rebuild_navigation()
        print("  Navigation rebuilt.")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error rebuilding navigation: {e.details()}")


# ---------------------------------------------------------------------------
# Main menu
# ---------------------------------------------------------------------------

TOP_LEVEL_ACTIONS = [
    ("list_cmd",     "List commandable pawns"),
    ("list_nav",     "List navigable pawns"),
    ("vehicle",      "Command vehicle (normalized acceleration/steering)"),
    ("velocity",     "Command velocity (twist)"),
    ("acceleration", "Command acceleration"),
    ("move_to",      "Pawn move-to-location (uses navigation)"),
    ("rebuild_nav",  "Rebuild navigation"),
    ("quit",         "Quit"),
]


async def main():
    parser = argparse.ArgumentParser(description="Movement Playground - interactive TempoMovement client")
    parser.add_argument('--ip', required=False, help="IP address of machine where Tempo is running", default="0.0.0.0")
    parser.add_argument('--port', required=False, help="Port Tempo gRPC server is using", default=10001)
    args = parser.parse_args()

    if args.ip != "0.0.0.0" or args.port != 10001:
        tempo.set_server(address=args.ip, port=args.port)

    print("\n=== Movement Playground ===")
    print("Drive vehicles, command velocities/accelerations, and navigate pawns at runtime.\n")

    while True:
        action = await fuzzy_select(TOP_LEVEL_ACTIONS, "What would you like to do?", allow_restart=False)

        if action == "quit" or action == RESTART_SENTINEL:
            print("\nBye!\n")
            break

        try:
            if action == "list_cmd":
                await flow_list_commandable()
            elif action == "list_nav":
                await flow_list_navigable()
            elif action == "vehicle":
                await flow_command_vehicle()
            elif action == "velocity":
                await flow_command_velocity()
            elif action == "acceleration":
                await flow_command_acceleration()
            elif action == "move_to":
                await flow_move_to_location()
            elif action == "rebuild_nav":
                await flow_rebuild_navigation()
        except grpc.aio._call.AioRpcError:
            print("\n  Could not connect to Tempo. Is the simulation running?")


if __name__ == "__main__":
    asyncio.run(main())
