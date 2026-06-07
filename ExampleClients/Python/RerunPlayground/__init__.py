# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Rerun visualization client for Tempo.

Streams every available sensor and the ground-truth world state into the
rerun viewer (https://rerun.io), auto-builds a viewer layout, and exposes a
small Gradio control panel for editing actor/component properties live over
the TempoWorld API.

Run with:  python -m RerunPlayground --auto
See README.md for details.
"""
