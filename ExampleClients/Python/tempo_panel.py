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

def _kind(property_type: str) -> str:
    t = property_type.lower()
    if "bool" in t:
        return "bool"
    if "float" in t or "double" in t:
        return "float"
    if "int" in t or "byte" in t or "uint" in t:
        return "int"
    return "string"


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
            out.append({"name": prop.name, "kind": _kind(prop.property_type),
                        "type": prop.property_type, "value": prop.value})
        return out

    def set_value(actor, component, name, kind, value):
        try:
            if kind == "bool":
                func, val = tw.set_bool_property, bool(value)
            elif kind == "float":
                func, val = tw.set_float_property, float(value)
            elif kind == "int":
                func, val = tw.set_int_property, int(value)
            elif component:
                func, val = tw.set_component_property, str(value)
            else:
                func, val = tw.set_actor_property, str(value)
            bridge.submit(func, actor=actor, component=component, property=name, value=val)
            return f"✓ set {name} = {value}"
        except Exception as exc:
            return f"✗ error setting {name}: {exc}"

    gr.Markdown(
        "### Properties\n"
        "Pick an actor and (optionally) a component, then **Load properties**. "
        "Edits apply on **Enter** or when you click away; checkboxes apply immediately. "
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
        for prop in props:
            name, kind, ptype, value = prop["name"], prop["kind"], prop["type"], prop["value"]
            label = f"{name}  ·  {ptype}"

            def make_callback(n=name, k=kind, a=actor, c=component):
                return lambda v: set_value(a, c, n, k, v)

            cb = make_callback()
            if kind == "bool":
                widget = gr.Checkbox(label=label, value=str(value).lower() in ("true", "1", "yes"))
                widget.change(cb, inputs=widget, outputs=status)
            else:
                # Numeric (float/int) and string fields all use a Textbox. It commits
                # on Enter and on blur, and avoids the number input's step validation
                # (which rejects e.g. 0.1 against the default integer step). set_value
                # converts the text back to the property's type by `kind`.
                widget = gr.Textbox(label=label, value=str(value))
                widget.submit(cb, inputs=widget, outputs=status)
                widget.blur(cb, inputs=widget, outputs=status)


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
