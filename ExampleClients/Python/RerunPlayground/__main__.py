# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Entrypoint: ``python -m RerunPlayground [--auto] [options]``.

Run from ExampleClients/Python (so ``RerunPlayground`` is importable), with the
Tempo API package on PYTHONPATH. See README.md.
"""

import asyncio

import tempo_sim

from .config import parse_config
from .discovery import discover_scene
from .client import TempoRerunClient


async def amain(cfg) -> None:
    loop = asyncio.get_running_loop()

    await tempo_sim.set_server(address=cfg.ip, port=cfg.port)
    print(f"\n=== Tempo × rerun ===\nConnecting to {cfg.ip}:{cfg.port} ...")

    scene = await discover_scene(cfg)
    if not scene.sensors and not scene.track_actor:
        print("  Nothing to visualize (no sensors and no actors). Is the simulation running?")
        return

    client = TempoRerunClient(cfg, scene, loop)
    await client.setup()
    client.start_streams()

    if cfg.control:
        try:
            from .control.panel import launch_control_panel
            launch_control_panel(loop, cfg, client)
        except Exception as exc:
            print(f"  Control panel unavailable ({exc}); continuing without it.")

    print("\nStreaming. Press Ctrl+C to stop.\n")
    try:
        await client.run()
    finally:
        await client.shutdown()


def main() -> None:
    cfg = parse_config()
    try:
        asyncio.run(amain(cfg))
    except KeyboardInterrupt:
        print("\nBye!\n")


if __name__ == "__main__":
    main()
