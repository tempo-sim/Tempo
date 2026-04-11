# Copyright Tempo Simulation, LLC. All Rights Reserved

import argparse
import asyncio
import math
import grpc

import tempo
import tempo.tempo_world as tw
import TempoScripting.Geometry_pb2 as Geometry

from prompt_toolkit import PromptSession
from prompt_toolkit.completion import FuzzyWordCompleter
from prompt_toolkit.formatted_text import HTML
from prompt_toolkit.shortcuts import radiolist_dialog


RESTART_SENTINEL = "__RESTART__"
QUIT_SENTINEL = "__QUIT__"
NONE_SENTINEL = "__NONE__"


# ---------------------------------------------------------------------------
# Fuzzy select helper
# ---------------------------------------------------------------------------

async def fuzzy_select(items, prompt_text, allow_restart=True, allow_none=False, none_label="(None - actor properties only)"):
    """Interactive fuzzy-filterable selection.

    items: list of (value, display_string) tuples
    Returns the selected value, RESTART_SENTINEL, or NONE_SENTINEL.
    """
    extra = []
    if allow_none:
        extra.append((NONE_SENTINEL, none_label))
    if allow_restart:
        extra.append((RESTART_SENTINEL, "< Restart >"))

    all_items = list(items) + extra

    if not all_items:
        print("  No items available.")
        return RESTART_SENTINEL

    # Build lookup from display string to value
    display_to_value = {}
    displays = []
    for value, display in all_items:
        display_to_value[display] = value
        displays.append(display)

    completer = FuzzyWordCompleter(displays)
    session = PromptSession()

    while True:
        # Show numbered list
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

        # Empty input -> select first item
        if user_input == "":
            return all_items[0][0]

        # Try as index
        try:
            idx = int(user_input)
            if 0 <= idx < len(all_items):
                return all_items[idx][0]
            else:
                print(f"  Index out of range (0-{len(all_items) - 1})")
                continue
        except ValueError:
            pass

        # Try exact match on display string
        if user_input in display_to_value:
            return display_to_value[user_input]

        # Try case-insensitive substring match
        matches = [(v, d) for v, d in all_items if user_input.lower() in d.lower()]
        if len(matches) == 1:
            return matches[0][0]
        elif len(matches) > 1:
            # Narrow the list and re-prompt
            all_items = matches
            display_to_value = {d: v for v, d in all_items}
            displays = [d for _, d in all_items]
            completer = FuzzyWordCompleter(displays)
            continue
        else:
            print(f"  No match for '{user_input}'. Try again.")


async def text_input(prompt_text, default=""):
    """Simple text prompt with an optional default."""
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
# Transform helper
# ---------------------------------------------------------------------------

def parse_transform(text):
    """Parse 'X Y Z R P Y' string into a Geometry.Transform (meters/degrees)."""
    parts = text.split()
    if len(parts) != 6:
        raise ValueError("Expected 6 values: X Y Z Roll Pitch Yaw")
    t = Geometry.Transform()
    t.location.x = float(parts[0])
    t.location.y = float(parts[1])
    t.location.z = float(parts[2])
    t.rotation.r = float(parts[3]) * math.pi / 180.0
    t.rotation.p = float(parts[4]) * math.pi / 180.0
    t.rotation.y = float(parts[5]) * math.pi / 180.0
    return t


def format_transform(t):
    """Format a Geometry.Transform for display."""
    return (f"Location({t.location.x:.2f}, {t.location.y:.2f}, {t.location.z:.2f}) "
            f"Rotation({t.rotation.r * 180 / math.pi:.1f}, {t.rotation.p * 180 / math.pi:.1f}, {t.rotation.y * 180 / math.pi:.1f})")


# ---------------------------------------------------------------------------
# Actor / Component fetchers
# ---------------------------------------------------------------------------

async def fetch_actors():
    """Returns list of (name, display_string) tuples."""
    try:
        response = await tw.get_all_actors()
        return [(a.name, f"{a.name} ({a.type})") for a in response.actors]
    except grpc.aio._call.AioRpcError as e:
        print(f"\n  Error fetching actors: {e.details()}")
        return []


async def fetch_components(actor):
    """Returns list of (name, display_string) tuples."""
    try:
        response = await tw.get_all_components(actor=actor)
        return [(c.name, f"{c.name} ({c.type})") for c in response.components]
    except grpc.aio._call.AioRpcError as e:
        print(f"\n  Error fetching components: {e.details()}")
        return []


async def fetch_properties(actor, component=None):
    """Returns list of PropertyDescriptor objects (filtering out unsupported)."""
    try:
        if component:
            response = await tw.get_component_properties(actor=actor, component=component)
        else:
            response = await tw.get_actor_properties(actor=actor)
        return [p for p in response.properties if p.type != "unsupported"]
    except grpc.aio._call.AioRpcError as e:
        print(f"\n  Error fetching properties: {e.details()}")
        return []


# ---------------------------------------------------------------------------
# Property setter dispatch
# ---------------------------------------------------------------------------

def parse_bool(text):
    if text.lower() in ("true", "1", "yes"):
        return True
    if text.lower() in ("false", "0", "no"):
        return False
    raise ValueError(f"Cannot parse '{text}' as bool. Use true/false, 1/0, or yes/no.")


async def set_property_value(actor, component, prop_name, prop_type, value_text):
    """Parse value_text according to prop_type and call the correct setter."""
    kwargs = dict(actor=actor, component=component, property=prop_name)

    try:
        t = prop_type.lower().strip()

        # Array types
        if t.endswith("[]"):
            base = t[:-2]
            return await _set_array_property(base, kwargs, value_text)

        # Scalar types
        if t == "bool":
            await tw.set_bool_property(**kwargs, value=parse_bool(value_text))
        elif t == "string":
            await tw.set_string_property(**kwargs, value=value_text)
        elif t == "enum":
            await tw.set_enum_property(**kwargs, value=value_text)
        elif t in ("int", "int32", "int64"):
            await tw.set_int_property(**kwargs, value=int(value_text))
        elif t in ("float", "double"):
            await tw.set_float_property(**kwargs, value=float(value_text))
        elif t == "vector":
            parts = value_text.split()
            if len(parts) != 3:
                raise ValueError("Vector requires 3 values: X Y Z")
            await tw.set_vector_property(**kwargs, x=float(parts[0]), y=float(parts[1]), z=float(parts[2]))
        elif t == "rotator":
            parts = value_text.split()
            if len(parts) != 3:
                raise ValueError("Rotator requires 3 values: Roll Pitch Yaw")
            await tw.set_rotator_property(**kwargs, r=float(parts[0]), p=float(parts[1]), y=float(parts[2]))
        elif t == "color":
            parts = value_text.split()
            if len(parts) != 3:
                raise ValueError("Color requires 3 values: R G B (0-255)")
            await tw.set_color_property(**kwargs, r=int(parts[0]), g=int(parts[1]), b=int(parts[2]))
        elif t == "class":
            await tw.set_class_property(**kwargs, value=value_text)
        elif t == "asset":
            await tw.set_asset_property(**kwargs, value=value_text)
        elif t == "actor":
            await tw.set_actor_property(**kwargs, value=value_text)
        elif t == "component":
            await tw.set_component_property(**kwargs, value=value_text)
        else:
            print(f"  Unsupported property type: {prop_type}. Attempting as string.")
            await tw.set_string_property(**kwargs, value=value_text)

        return True
    except grpc.aio._call.AioRpcError as e:
        print(f"  RPC Error: {e.details()}")
        return False
    except (ValueError, IndexError) as e:
        print(f"  Parse error: {e}")
        return False


async def _set_array_property(base_type, kwargs, value_text):
    """Set an array property. Values are comma-separated."""
    parts = [v.strip() for v in value_text.split(",")]

    if base_type == "bool":
        await tw.set_bool_array_property(**kwargs, values=[parse_bool(v) for v in parts])
    elif base_type == "string":
        await tw.set_string_array_property(**kwargs, values=parts)
    elif base_type == "enum":
        await tw.set_enum_array_property(**kwargs, values=parts)
    elif base_type in ("int", "int32", "int64"):
        await tw.set_int_array_property(**kwargs, values=[int(v) for v in parts])
    elif base_type in ("float", "double"):
        await tw.set_float_array_property(**kwargs, values=[float(v) for v in parts])
    elif base_type == "class":
        await tw.set_class_array_property(**kwargs, values=parts)
    elif base_type == "asset":
        await tw.set_asset_array_property(**kwargs, values=parts)
    elif base_type == "actor":
        await tw.set_actor_array_property(**kwargs, values=parts)
    elif base_type == "component":
        await tw.set_component_array_property(**kwargs, values=parts)
    else:
        print(f"  Unsupported array type: {base_type}[]. Attempting as string array.")
        await tw.set_string_array_property(**kwargs, values=parts)

    return True


# ---------------------------------------------------------------------------
# Flows
# ---------------------------------------------------------------------------

async def flow_spawn_actor():
    print("\n--- Spawn Actor ---")
    actor_type = await text_input("Actor type (e.g. BP_SensorRig)")
    if not actor_type:
        print("  No type provided, cancelled.")
        return

    transform_text = await text_input("Transform (X Y Z Roll Pitch Yaw, degrees)", default="0 0 0 0 0 0")
    try:
        transform = parse_transform(transform_text)
    except ValueError as e:
        print(f"  {e}")
        return

    try:
        response = await tw.spawn_actor(type=actor_type, transform=transform)
        print(f"\n  Spawned: {response.spawned_name}")
        print(f"  Transform: {format_transform(response.spawned_transform)}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error: {e.details()}")


async def flow_destroy_actor():
    print("\n--- Destroy Actor ---")
    actors = await fetch_actors()
    if not actors:
        print("  No actors found.")
        return

    selection = await fuzzy_select(actors, "Select actor to destroy")
    if selection == RESTART_SENTINEL:
        return

    try:
        await tw.destroy_actor(actor=selection)
        print(f"\n  Destroyed: {selection}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error: {e.details()}")


async def flow_add_component():
    print("\n--- Add Component ---")

    component_type = await text_input("Component type (e.g. TempoCamera)")
    if not component_type:
        print("  No type provided, cancelled.")
        return

    actors = await fetch_actors()
    if not actors:
        print("  No actors found.")
        return

    actor = await fuzzy_select(actors, "Select owner actor")
    if actor == RESTART_SENTINEL:
        return

    name = await text_input("Component name (optional, leave empty for auto)")

    components = await fetch_components(actor)
    parent = ""
    if components:
        parent_selection = await fuzzy_select(
            components, "Select parent component",
            allow_none=True, none_label="(Root Component)")
        if parent_selection == RESTART_SENTINEL:
            return
        if parent_selection == NONE_SENTINEL:
            parent = ""
        else:
            parent = parent_selection

    try:
        response = await tw.add_component(type=component_type, actor=actor, name=name, parent=parent)
        print(f"\n  Added component: {response.name}")
        print(f"  Transform: {format_transform(response.transform)}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error: {e.details()}")


async def flow_destroy_component():
    print("\n--- Destroy Component ---")
    actors = await fetch_actors()
    if not actors:
        print("  No actors found.")
        return

    actor = await fuzzy_select(actors, "Select actor")
    if actor == RESTART_SENTINEL:
        return

    components = await fetch_components(actor)
    if not components:
        print(f"  No components found on {actor}.")
        return

    component = await fuzzy_select(components, "Select component to destroy")
    if component == RESTART_SENTINEL:
        return

    try:
        await tw.destroy_component(actor=actor, component=component)
        print(f"\n  Destroyed: {actor}:{component}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error: {e.details()}")


async def flow_set_property():
    print("\n--- Set Property ---")

    # Step 1: Select actor
    actors = await fetch_actors()
    if not actors:
        print("  No actors found.")
        return

    actor = await fuzzy_select(actors, "Select actor")
    if actor == RESTART_SENTINEL:
        return

    # Step 2: Select component or actor-level
    components = await fetch_components(actor)
    component = None
    if components:
        comp_selection = await fuzzy_select(
            components, "Select component",
            allow_none=True, none_label="(Actor properties only)")
        if comp_selection == RESTART_SENTINEL:
            return
        if comp_selection != NONE_SENTINEL:
            component = comp_selection

    # Step 3: Fetch and select property
    properties = await fetch_properties(actor, component)
    if not properties:
        target = f"{actor}:{component}" if component else actor
        print(f"  No settable properties found on {target}.")
        return

    prop_items = [(p.name, f"{p.name} ({p.type}) = {p.value}") for p in properties]
    prop_name = await fuzzy_select(prop_items, "Select property")
    if prop_name == RESTART_SENTINEL:
        return

    # Find the selected property descriptor
    prop = next(p for p in properties if p.name == prop_name)

    # Step 4: Display current value and prompt for new value
    print(f"\n  Property: {prop.name}")
    print(f"  Type:     {prop.type}")
    print(f"  Current:  {prop.value}")

    type_hint = ""
    t = prop.type.lower().strip()
    if t == "bool":
        type_hint = " (true/false)"
    elif t == "vector":
        type_hint = " (X Y Z)"
    elif t == "rotator":
        type_hint = " (Roll Pitch Yaw)"
    elif t == "color":
        type_hint = " (R G B, 0-255)"
    elif t.endswith("[]"):
        type_hint = " (comma-separated values)"

    new_value = await text_input(f"New value{type_hint}", default=prop.value)

    # Step 5: Set the property
    success = await set_property_value(
        actor=actor,
        component=component or "",
        prop_name=prop_name,
        prop_type=prop.type,
        value_text=new_value,
    )

    if success:
        print(f"\n  Successfully set {prop.name} = {new_value}")


# ---------------------------------------------------------------------------
# Main menu
# ---------------------------------------------------------------------------

TOP_LEVEL_ACTIONS = [
    ("spawn",     "Spawn Actor"),
    ("destroy",   "Destroy Actor"),
    ("add_comp",  "Add Component"),
    ("del_comp",  "Destroy Component"),
    ("set_prop",  "Set Property"),
    ("quit",      "Quit"),
]


async def main():
    parser = argparse.ArgumentParser(description="TempoWorld Interactive CLI")
    parser.add_argument('--ip', required=False, help="IP address of machine where Tempo is running", default="0.0.0.0")
    parser.add_argument('--port', required=False, help="Port Tempo scripting server is using", default=10001)
    args = parser.parse_args()

    if args.ip != "0.0.0.0" or args.port != 10001:
        tempo.set_server(address=args.ip, port=args.port)

    print("\n=== TempoWorld Interactive Client ===")
    print("Control actors, components, and properties at runtime.\n")

    while True:
        action = await fuzzy_select(TOP_LEVEL_ACTIONS, "What would you like to do?", allow_restart=False)

        if action == "quit" or action == RESTART_SENTINEL:
            print("\nBye!\n")
            break

        try:
            if action == "spawn":
                await flow_spawn_actor()
            elif action == "destroy":
                await flow_destroy_actor()
            elif action == "add_comp":
                await flow_add_component()
            elif action == "del_comp":
                await flow_destroy_component()
            elif action == "set_prop":
                await flow_set_property()
        except grpc.aio._call.AioRpcError:
            print("\n  Could not connect to Tempo. Is the simulation running?")


if __name__ == "__main__":
    asyncio.run(main())
