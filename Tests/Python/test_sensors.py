# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Integration tests for the TempoSensors data APIs against a packaged sim.

These actually render, so they need a GPU and the sim launched with TEMPO_SIM_RENDER=1 (off-screen
rendering) — see conftest.py. They're in the `sensors` group, which is intentionally NOT part of
the default CI matrix (no GPU on the default runners). Enable by adding `sensors` to the matrix in
tempo_build_and_package.yml with a GPU `runner` and `setup_render: true` (TEMPO_SIM_RENDER=1).

Locally:  TEMPO_SIM_RENDER=1 Scripts/TestPython.sh sensors
"""

import pytest

import tempo_sim.tempo_world as tw
import tempo_sim.tempo_sensors as ts

pytestmark = pytest.mark.sensors

# TempoSample's reference rig (a tripod with a TempoCamera on top) — the same actor the README
# hello-world uses.
SENSOR_RIG_TYPE = "BP_SensorRig"


def test_stream_one_color_image(sim_server):
    spawned = tw.spawn_actor(actor_type=SENSOR_RIG_TYPE)
    owner = spawned.name
    assert owner, f"could not spawn {SENSOR_RIG_TYPE}"

    try:
        sensors = ts.get_available_sensors().available_sensors
        cam = next((s for s in sensors if s.owner == owner), None)
        if cam is None:
            pytest.skip(f"No sensor reported on {owner}; expected a camera on {SENSOR_RIG_TYPE}.")

        # First frame off the stream. The loop-rebinding caveat (see the gRPC channel notes) only
        # affects *subsequent* frames via the sync wrapper, so taking just the first is safe.
        image = next(iter(ts.stream_color_images(owner=cam.owner, sensor=cam.name)))

        assert image.width_px > 0 and image.height_px > 0, "image has zero dimensions"
        assert len(image.data) > 0, "image carried no pixel data"
    finally:
        tw.destroy_actor(actor=owner)
