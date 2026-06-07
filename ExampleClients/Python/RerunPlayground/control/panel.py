# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Rerun control panel: stream controls + the shared TempoWorld property editor.

The property editor (actor/component get/set) is the reusable `tempo_panel`
building block, also used by the standalone WorldPlaygroundGUI.py. This module
adds the rerun-specific Streams section (lidar signal + per-camera image) and
launches the page.

See tempo_panel for the threading/async details; the short version is that
launch() must run on the main thread (Gradio initializes its queue via an HTTP
self-request during launch) and building the UI makes no API calls.
"""

import traceback

import gradio as gr

from tempo_panel import AsyncBridge, property_editor, find_free_port

from ..client import LIDAR_SIGNALS

# Image kinds the camera dropdown offers (bbox is an overlay, not a standalone view).
CAMERA_IMAGE_KINDS = ("color", "video", "depth", "label")
CAMERA_OFF = "(off)"

_bridge = None  # AsyncBridge onto the main loop; set by launch_control_panel
_client = None  # TempoRerunClient; set by launch_control_panel


def _set_lidar_mode(key, mode) -> str:
    try:
        _bridge.submit(_client.set_lidar_mode, key, mode)
        return f"✓ {key} → {mode}"
    except Exception as exc:
        return f"✗ {exc}"


def _set_camera_image(key, choice) -> str:
    kinds = [] if choice == CAMERA_OFF else [choice]
    try:
        _bridge.submit(_client.set_camera_measurements, key, kinds)
        return f"✓ {key}: {choice}"
    except Exception as exc:
        return f"✗ {exc}"


def _streams_section():
    gr.Markdown(
        "Pick which lidar signal to display, and which image each camera streams. "
        "Changing these re-applies the generated layout."
    )
    stream_status = gr.Markdown("")
    for sensor in _client.scene.sensors:
        if sensor.is_lidar:
            dd = gr.Dropdown(
                choices=list(LIDAR_SIGNALS),
                value=_client.lidar_modes.get(sensor.key, _client.cfg.colorize_lidar_by),
                label=f"{sensor.key} — lidar signal",
            )
            dd.change(lambda m, k=sensor.key: _set_lidar_mode(k, m), inputs=dd, outputs=stream_status)
        elif sensor.is_camera:
            available = [k for k in CAMERA_IMAGE_KINDS if k in _client.camera_kinds_available(sensor)]
            current = next((k for k in available if f"{sensor.key}:{k}" in _client.tasks), CAMERA_OFF)
            dd = gr.Dropdown(choices=[CAMERA_OFF] + available, value=current, label=f"{sensor.key} — image")
            dd.change(lambda choice, k=sensor.key: _set_camera_image(k, choice),
                      inputs=dd, outputs=stream_status)


def _build_demo(cfg):
    with gr.Blocks(title="Tempo × rerun control") as demo:
        gr.Markdown("## Tempo × rerun — control panel")
        with gr.Accordion("Streams", open=True):
            _streams_section()
        property_editor(_bridge, demo)
    return demo


def launch_control_panel(loop, cfg, client) -> None:
    """Launch the Gradio panel (non-blocking) on the main thread.

    Runs on the main thread so Gradio's queue self-request works; building makes
    no API calls (the actor list loads lazily), so there's nothing to marshal onto
    the not-yet-running loop. See tempo_panel for the full rationale.
    """
    global _bridge, _client
    _bridge = AsyncBridge(loop)
    _client = client
    try:
        demo = _build_demo(cfg)
        demo.queue()
        port = find_free_port(cfg.control_port)
        if port != cfg.control_port:
            print(f"  Port {cfg.control_port} busy; using {port}")
        demo.launch(server_port=port, show_error=True, inbrowser=False,
                    quiet=True, prevent_thread_lock=True)
        print(f"  Control panel: http://localhost:{port}")
    except Exception:
        print("  [control] panel failed to start:\n" + traceback.format_exc())
