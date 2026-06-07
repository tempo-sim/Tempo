# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Reusable Gradio building blocks for TempoWorld property editing.

This is shared by two example clients:
  - WorldPlaygroundGUI.py — a standalone get/set property GUI for any running sim.
  - RerunPlayground       — embeds the same property editor next to its stream controls.

The pieces:
  - ``AsyncBridge``     — marshals API calls onto an asyncio loop (see note below).
  - ``property_editor`` — builds the actor/component/property editor into a gr.Blocks.
  - ``launch``          — standalone convenience: connect, build, serve.

Async note: the Tempo API uses curio's ``@awaitable``, which dispatches sync-vs-async
based on the *calling frame*. Calling it from a Gradio worker thread takes the sync
path (spinning a throwaway loop, rebuilding the gRPC channel). ``AsyncBridge.submit``
instead awaits the call inside a coroutine running on a single, long-lived loop, so it
dispatches async and reuses one channel.

Gradio note: the queue initializes via an HTTP self-request to /startup-events during
``launch()``, which must run on the main thread — so build makes no API calls (the
actor list is filled lazily via ``demo.load``), and launch happens on the main thread.
"""

import asyncio
import socket
import threading

import gradio as gr

import tempo_sim
import tempo_sim.tempo_world as tw


class AsyncBridge:
    """Run Tempo API calls on a specific asyncio loop from any (Gradio) thread."""

    def __init__(self, loop):
        self.loop = loop

    def submit(self, func, *args, timeout: float = 15.0, **kwargs):
        async def runner():
            return await func(*args, **kwargs)

        return asyncio.run_coroutine_threadsafe(runner(), self.loop).result(timeout=timeout)


def find_free_port(start: int, attempts: int = 50) -> int:
    """Return the first free TCP port at/after `start` (so a second panel doesn't
    collide with one already serving). Falls back to `start` if none found."""
    for port in range(start, start + attempts):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                sock.bind(("127.0.0.1", port))
                return port
            except OSError:
                continue
    return start


def run_background_loop():
    """Start a fresh asyncio loop in a daemon thread and return it.

    Used by standalone callers that have no event loop of their own; the Tempo
    gRPC channel binds to this loop (all API calls are marshalled onto it).
    """
    loop = asyncio.new_event_loop()

    def _run():
        asyncio.set_event_loop(loop)
        loop.run_forever()

    threading.Thread(target=_run, daemon=True, name="tempo-api-loop").start()
    return loop


# ---------------------------------------------------------------------------
# Property helpers (pure)
# ---------------------------------------------------------------------------
#
# TempoWorld returns each property's current value as a formatted string. Scalar
# values come back bare ("1.5", "true", "SomeName"); structs and arrays come
# back as Unreal-style text — "(X=1.0,Y=2.0,Z=3.0)" or "(a,b,c)". To edit one
# member of a struct or one element of an array, the typed setters accept a
# dotted property path (e.g. "RelativeLocation.X" or "Waypoints.2"), so we parse
# the value string to discover members/elements and recurse into them. This
# mirrors how WorldPlayground.py (the CLI) supports structs.

# Types that can be set directly with one typed setter (not drilled into).
LEAF_TYPES = {
    "bool", "string", "enum", "int", "int32", "int64", "float", "double",
    "vector", "rotator", "color", "class", "asset", "actor", "component",
}


def _is_leaf_type(property_type: str) -> bool:
    """True if `property_type` (or an array of one) can be set in a single call."""
    t = property_type.lower().strip()
    if t in LEAF_TYPES:
        return True
    if t.endswith("[]") and t[:-2] in LEAF_TYPES:
        return True
    return False


def _split_top_level(s, delimiter=","):
    """Split `s` on `delimiter`, ignoring delimiters nested inside parens/braces."""
    parts, depth, current = [], 0, []
    for ch in s:
        if ch in ("(", "{"):
            depth += 1
            current.append(ch)
        elif ch in (")", "}"):
            depth -= 1
            current.append(ch)
        elif ch == delimiter and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    tail = "".join(current).strip()
    if tail:
        parts.append(tail)
    return parts


def _is_composite_value(value_str: str) -> bool:
    """True if the value string looks like a struct/array — "(...)" or "{...}"."""
    v = value_str.strip()
    return ((v.startswith("(") and v.endswith(")")) or
            (v.startswith("{") and v.endswith("}")))


def _unwrap_value(value_str):
    """Strip outer delimiters, returning (inner, member_separator).

    Supports "(K=V,...)" (Unreal struct text) and "{K:V, ...}" (JSON-ish).
    Returns ("", "") if neither format wraps the value.
    """
    v = value_str.strip()
    if v.startswith("(") and v.endswith(")"):
        return v[1:-1], "="
    if v.startswith("{") and v.endswith("}"):
        return v[1:-1], ":"
    return "", ""


def _infer_leaf_type(value_str: str) -> str:
    """Infer a settable scalar type ("bool"/"int"/"float"/"string") for a member.

    Struct members and array elements arrive without type info, so we guess from
    the value text (the CLI does the same).
    """
    v = value_str.strip()
    if v.lower() in ("true", "false"):
        return "bool"
    try:
        int(v)
        return "int"
    except ValueError:
        pass
    try:
        float(v)
        return "float"
    except ValueError:
        pass
    return "string"


def _parse_struct_value(value_str):
    """Parse "(X=1,Y=2)" / "{a:1}" into an ordered {member_name: value_str} dict.

    Returns {} if the value isn't a key/value struct.
    """
    inner, sep = _unwrap_value(value_str)
    if not inner or not sep:
        return {}
    members = {}
    for part in _split_top_level(inner, ","):
        pos = part.find(sep)
        if pos == -1:
            continue
        name = part[:pos].strip()
        if name:
            members[name] = part[pos + 1:].strip()
    return members


def _parse_array_value(value_str):
    """Parse "(a,b,c)" / "{a, b}" into a list of element value strings.

    Returns [] if the value is a key/value struct rather than a flat array.
    """
    inner, sep = _unwrap_value(value_str)
    if not inner:
        return []
    elements = []
    for part in _split_top_level(inner, ","):
        val = part.strip()
        # A "K=V" / "K:V" element means this is a struct, not an array.
        if sep and sep in val and not _is_composite_value(val):
            return []
        elements.append(val)
    return elements


def _type_hint(property_type: str) -> str:
    """A short parenthetical telling the user the expected input format."""
    t = property_type.lower().strip()
    if t == "vector":
        return " (X Y Z)"
    if t == "rotator":
        return " (Roll Pitch Yaw)"
    if t == "color":
        return " (R G B, 0-255)"
    if t.endswith("[]"):
        return " (comma-separated)"
    return ""


def _parse_bool(text: str) -> bool:
    t = text.strip().lower()
    if t in ("true", "1", "yes"):
        return True
    if t in ("false", "0", "no"):
        return False
    raise ValueError(f"cannot parse '{text}' as bool (use true/false)")


def _error_text(exc) -> str:
    """Best-effort human-readable message from a gRPC or parsing error."""
    details = getattr(exc, "details", None)
    if callable(details):
        try:
            return details()
        except Exception:
            pass
    return str(exc)


def apply_property(submit, actor, component, name, property_type, value_text):
    """Parse `value_text` per `property_type` and call the matching typed setter
    through `submit` (e.g. ``AsyncBridge.submit``). Returns a status string.

    `property_type` is a TempoWorld type ("vector", "float[]", ...) for a
    top-level property, or an inferred scalar type for a struct member / array
    element reached by a dotted `name` path. Mirrors WorldPlayground.py's
    ``set_property_value`` so the GUI and CLI accept the same inputs.
    """
    kwargs = dict(actor=actor, component=component, property=name)
    try:
        t = property_type.lower().strip()

        # Array types — comma-separated values.
        if t.endswith("[]"):
            _apply_array_property(submit, t[:-2], kwargs, value_text)
        elif t == "bool":
            submit(tw.set_bool_property, **kwargs, value=_parse_bool(value_text))
        elif t == "string":
            submit(tw.set_string_property, **kwargs, value=value_text)
        elif t == "enum":
            submit(tw.set_enum_property, **kwargs, value=value_text)
        elif t in ("int", "int32", "int64"):
            submit(tw.set_int_property, **kwargs, value=int(value_text))
        elif t in ("float", "double"):
            submit(tw.set_float_property, **kwargs, value=float(value_text))
        elif t == "vector":
            x, y, z = _three(value_text, "Vector requires 3 values: X Y Z")
            submit(tw.set_vector_property, **kwargs, x=float(x), y=float(y), z=float(z))
        elif t == "rotator":
            r, p, y = _three(value_text, "Rotator requires 3 values: Roll Pitch Yaw")
            submit(tw.set_rotator_property, **kwargs, r=float(r), p=float(p), y=float(y))
        elif t == "color":
            r, g, b = _three(value_text, "Color requires 3 values: R G B (0-255)")
            submit(tw.set_color_property, **kwargs, r=int(r), g=int(g), b=int(b))
        elif t == "class":
            submit(tw.set_class_property, **kwargs, value=value_text)
        elif t == "asset":
            submit(tw.set_asset_property, **kwargs, value=value_text)
        elif t == "actor":
            submit(tw.set_actor_property, **kwargs, value=value_text)
        elif t == "component":
            submit(tw.set_component_property, **kwargs, value=value_text)
        else:
            # Unknown scalar — best effort as a string (matches the CLI).
            submit(tw.set_string_property, **kwargs, value=value_text)

        return f"✓ set {name} = {value_text}"
    except Exception as exc:  # gRPC errors, parse errors
        return f"✗ error setting {name}: {_error_text(exc)}"


def _three(value_text, message):
    parts = value_text.split()
    if len(parts) != 3:
        raise ValueError(message)
    return parts


def _apply_array_property(submit, base_type, kwargs, value_text):
    """Set a whole array property from comma-separated `value_text`."""
    parts = [v.strip() for v in value_text.split(",")]
    if base_type == "bool":
        submit(tw.set_bool_array_property, **kwargs, values=[_parse_bool(v) for v in parts])
    elif base_type == "string":
        submit(tw.set_string_array_property, **kwargs, values=parts)
    elif base_type == "enum":
        submit(tw.set_enum_array_property, **kwargs, values=parts)
    elif base_type in ("int", "int32", "int64"):
        submit(tw.set_int_array_property, **kwargs, values=[int(v) for v in parts])
    elif base_type in ("float", "double"):
        submit(tw.set_float_array_property, **kwargs, values=[float(v) for v in parts])
    elif base_type == "class":
        submit(tw.set_class_array_property, **kwargs, values=parts)
    elif base_type == "asset":
        submit(tw.set_asset_array_property, **kwargs, values=parts)
    elif base_type == "actor":
        submit(tw.set_actor_array_property, **kwargs, values=parts)
    elif base_type == "component":
        submit(tw.set_component_array_property, **kwargs, values=parts)
    else:
        submit(tw.set_string_array_property, **kwargs, values=parts)


# ---------------------------------------------------------------------------
# Property editor block
# ---------------------------------------------------------------------------

def property_editor(bridge: AsyncBridge, demo) -> None:
    """Build the actor/component/property editor inside the current gr.Blocks.

    `demo` is the enclosing Blocks (used to wire the lazy actor-list load).
    """

    def list_actors():
        try:
            return [a.name for a in bridge.submit(tw.get_all_actors).actors]
        except Exception as exc:
            print(f"  [panel] could not list actors: {exc}")
            return []

    def list_components(actor):
        if not actor:
            return []
        try:
            return [c.name for c in bridge.submit(tw.get_all_components, actor=actor).components]
        except Exception as exc:
            print(f"  [panel] could not list components: {exc}")
            return []

    def load_properties(actor, component, flt):
        if not actor:
            return []
        try:
            if component:
                resp = bridge.submit(tw.get_component_properties, actor=actor, component=component)
            else:
                resp = bridge.submit(tw.get_actor_properties, actor=actor)
        except Exception as exc:
            print(f"  [panel] could not load properties: {exc}")
            return []
        out = []
        needle = (flt or "").lower()
        for prop in resp.properties:
            if prop.property_type == "unsupported":
                continue
            if needle and needle not in prop.name.lower():
                continue
            out.append({"name": prop.name, "type": prop.property_type, "value": prop.value})
        return out

    gr.Markdown(
        "### Properties\n"
        "Pick an actor and (optionally) a component, then **Load properties**. "
        "Edits apply on **Enter** or when you click away; checkboxes apply immediately. "
        "Structs and arrays expand into editable members — open the accordion to "
        "edit one field (e.g. just the `X` of a location). "
        "Tip: filter by `distort` to find a camera's lens-distortion parameters."
    )
    with gr.Row():
        # Filled lazily via demo.load (below) so build makes no API calls.
        actor_dd = gr.Dropdown(label="Actor", choices=[], interactive=True)
        comp_dd = gr.Dropdown(label="Component (blank = the actor itself)", choices=[], interactive=True)
        refresh_btn = gr.Button("↻ Refresh actors", scale=0)
    with gr.Row():
        filter_tb = gr.Textbox(label="Filter properties", placeholder="e.g. distort", scale=3)
        load_btn = gr.Button("Load properties", variant="primary", scale=1)

    status = gr.Markdown("")
    props_state = gr.State([])

    refresh_btn.click(lambda: gr.update(choices=list_actors()), outputs=actor_dd)
    actor_dd.change(lambda a: (gr.update(choices=list_components(a), value=""), []),
                    inputs=actor_dd, outputs=[comp_dd, props_state])
    comp_dd.change(lambda: [], outputs=props_state)
    load_btn.click(load_properties, inputs=[actor_dd, comp_dd, filter_tb], outputs=props_state)
    demo.load(lambda: gr.update(choices=list_actors()), outputs=actor_dd)

    @gr.render(inputs=[actor_dd, comp_dd, props_state])
    def render_props(actor, component, props):
        if not props:
            gr.Markdown("_No properties loaded. Choose an actor/component and click **Load properties**._")
            return

        def render_leaf(path, base_label, ptype, value_str):
            """One editable widget that sets `path` as type `ptype` via a dotted path."""
            label = f"{base_label}  ·  {ptype}{_type_hint(ptype)}"

            def cb(value, p=path, t=ptype):
                return apply_property(bridge.submit, actor, component, p, t, value)

            if ptype.lower().strip() == "bool":
                widget = gr.Checkbox(label=label, value=str(value_str).strip().lower() in ("true", "1", "yes"))
                widget.change(cb, inputs=widget, outputs=status)
            else:
                # Numeric, string, vector/rotator/color, and array fields all use a
                # Textbox. It commits on Enter and on blur, and avoids the number
                # input's step validation (which rejects e.g. 0.1 against the default
                # integer step). apply_property parses the text back to `ptype`.
                widget = gr.Textbox(label=label, value=str(value_str))
                widget.submit(cb, inputs=widget, outputs=status)
                widget.blur(cb, inputs=widget, outputs=status)

        def render_node(path, label, ptype, value_str):
            """Render a property (or struct member / array element) recursively.

            `ptype` is the TempoWorld type for a top-level property, or None for a
            nested member/element whose type we infer from its value text.
            """
            # Leaf types (scalars and leaf-typed arrays) are set in a single widget.
            if ptype is not None and _is_leaf_type(ptype):
                render_leaf(path, label, ptype, value_str)
                return

            # Otherwise try to drill into the struct/array value text.
            if _is_composite_value(value_str):
                members = _parse_struct_value(value_str)
                if members:
                    with gr.Accordion(f"{label}  ·  {ptype or 'struct'}", open=False):
                        for mname, mvalue in members.items():
                            render_node(f"{path}.{mname}", mname, None, mvalue)
                    return
                elements = _parse_array_value(value_str)
                if elements:
                    with gr.Accordion(f"{label}  ·  {ptype or 'array'} [{len(elements)}]", open=False):
                        for i, evalue in enumerate(elements):
                            render_node(f"{path}.{i}", f"[{i}]", None, evalue)
                    return

            # A nested scalar (infer its type) or an unparseable composite
            # (fall back to setting the whole value as a string).
            leaf_type = ptype if ptype is not None else _infer_leaf_type(value_str)
            render_leaf(path, label, leaf_type, value_str)

        for prop in props:
            render_node(prop["name"], prop["name"], prop["type"], prop["value"])


# ---------------------------------------------------------------------------
# Standalone launch
# ---------------------------------------------------------------------------

def launch(address: str = "localhost", port: int = 10001,
           control_port: int = 7860, title: str = "TempoWorld panel") -> None:
    """Connect to Tempo and serve a standalone property-editor page (blocking)."""
    loop = run_background_loop()
    bridge = AsyncBridge(loop)
    bridge.submit(tempo_sim.set_server, address=address, port=port)

    with gr.Blocks(title=title) as demo:
        gr.Markdown(f"## {title}")
        property_editor(bridge, demo)

    demo.queue()
    port = find_free_port(control_port)
    if port != control_port:
        print(f"  Port {control_port} busy; using {port}")
    print(f"  Control panel: http://localhost:{port}")
    # Blocks on the main thread (required for Gradio's queue/startup self-request).
    demo.launch(server_port=port, show_error=True, inbrowser=False)
