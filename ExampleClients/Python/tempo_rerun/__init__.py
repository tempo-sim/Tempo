# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Shared building blocks for Tempo's rerun (https://rerun.io) example clients.

- ``streaming.pump``: drain a gRPC stream into a bounded drop-oldest queue and handle it on a
  worker thread, so the sim is never backpressured and the event loop never blocks.
- ``loggers.image`` / ``loggers.lidar`` / ``loggers.world``: one coroutine per stream kind. Each
  logs to rerun (``visualize``) and/or hands every message to ``taps`` (e.g. a recorder), so one
  stream serves both.
- ``sensors.SensorInfo``: what a logger needs to know about a sensor.
- ``labels.LabelRemap``: instance -> semantic id lookup for a sim in Instance label mode.
- ``blueprint.build_blueprint``: a viewer layout from the active streams.
- ``conventions`` / ``colormap`` / ``_compat``: frames and entity paths, palettes, and shims over
  the rerun calls that moved between releases.

Used by RerunPlayground (visualize whatever is in the scene) and CitySampleAV (a turnkey AV
sensor rig). Neither is installable; each launcher puts ExampleClients/Python on sys.path.
"""
