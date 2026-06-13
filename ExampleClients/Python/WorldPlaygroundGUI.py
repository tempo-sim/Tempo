# Copyright Tempo Simulation, LLC. All Rights Reserved

"""A standalone Gradio GUI for inspecting and editing TempoWorld properties.

The graphical counterpart to WorldPlayground.py (the interactive CLI): pick an
actor and (optionally) a component, load its properties, and edit them live.

Run it:  python ExampleClients/Python/WorldPlaygroundGUI.py [--ip ...] [--port ...]
then open the printed http://localhost:<port> URL.

Requires gradio (pip install 'tempo-sim[examples]' or the repo requirements.txt).
"""

import argparse

import tempo_panel


def main():
    parser = argparse.ArgumentParser(description="Gradio GUI for TempoWorld get/set properties.")
    parser.add_argument("--ip", default=None,
                        help="Tempo server IP. If unset, connect via Unix domain socket.")
    parser.add_argument("--port", type=int, default=10001, help="Tempo gRPC port (default: 10001).")
    parser.add_argument("--control-port", type=int, default=7860,
                        help="Port to serve the GUI on (default: 7860).")
    args = parser.parse_args()

    print("\n=== World Playground GUI ===")
    if args.ip is not None:
        print(f"Connecting to {args.ip}:{args.port} ...")
    else:
        print(f"Connecting via Unix domain socket (port key {args.port}) ...")
    try:
        tempo_panel.launch(address=args.ip, port=args.port, control_port=args.control_port,
                           title="TempoWorld — property editor")
    except KeyboardInterrupt:
        print("\nBye!\n")


if __name__ == "__main__":
    main()
