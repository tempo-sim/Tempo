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


async def _first_messages(stream, count):
    """The first count messages of stream, by sequence id (and, for a lidar's segments, where each starts)."""
    messages = {}
    async for message in stream:
        start = message.azimuth_range_rad.min if hasattr(message, "azimuth_range_rad") else 0.0
        messages[(message.header.sequence_id, start)] = message
        if len(messages) >= count:
            break
    return messages


async def _shared_memory_against_inline(owner, sensor, wrapper, rpc, request):
    import asyncio

    import tempo_sim.tempo_core as tc
    import tempo_sim.TempoSensors.Sensors_pb2_grpc as Sensors_grpc
    from tempo_sim._tempo_context import tempo_context

    await tc.set_console_variable(name="Tempo.WriteStats", value="1")
    # A default lidar's segments are smaller than the default threshold.
    await tc.set_console_variable(name="Tempo.SharedMemory.MinBytes", value="1024")
    try:
        # The wrapper asks for shared memory; a raw stub, which sends no metadata, gets every payload inline.
        stub = await tempo_context().get_stub(Sensors_grpc.SensorServiceStub)
        call = getattr(stub, rpc)(request)
        shared = wrapper(owner=owner, sensor=sensor)
        try:
            shared_messages, inline_messages = await asyncio.wait_for(asyncio.gather(
                _first_messages(shared, 6), _first_messages(call, 6)), timeout=120)
        finally:
            call.cancel()
            await shared.aclose()
        response = await tc.exec_console_command(command="Tempo.WriteStats.Dump")
    finally:
        await tc.set_console_variable(name="Tempo.WriteStats", value="0")
        await tc.set_console_variable(name="Tempo.SharedMemory.MinBytes", value="65536")
    return shared_messages, inline_messages, response.output


def _check_shared_memory_against_inline(shared_messages, inline_messages, stats_output):
    import json

    stats = json.loads(stats_output.strip().splitlines()[-1])
    assert stats["shm_messages"] > 0, f"nothing went through shared memory: {stats}"
    common = sorted(set(shared_messages) & set(inline_messages))
    assert common, "the two streams shared no capture"
    for sequence_id in common:
        shared, inline = shared_messages[sequence_id], inline_messages[sequence_id]
        # When each stream's response was placed on the wire may differ; nothing else may.
        shared.header.ClearField("transmission_time_s")
        inline.header.ClearField("transmission_time_s")
        assert shared == inline, \
            f"capture {sequence_id} differs between shared memory and inline"


def test_shared_memory_color_images_match_inline(sim_server):
    import asyncio
    import tempo_sim.TempoSensors.Camera_pb2 as Camera

    spawned = tw.spawn_actor(actor_type=SENSOR_RIG_TYPE)
    owner = spawned.name
    try:
        sensors = ts.get_available_sensors().available_sensors
        cam = next((s for s in sensors if s.owner == owner), None)
        if cam is None:
            pytest.skip(f"No sensor reported on {owner}; expected a camera on {SENSOR_RIG_TYPE}.")
        _check_shared_memory_against_inline(*asyncio.run(_shared_memory_against_inline(
            cam.owner, cam.name, ts.stream_color_images, "StreamColorImages",
            Camera.ColorImageRequest(owner=cam.owner, sensor=cam.name))))
    finally:
        tw.destroy_actor(actor=owner)


def test_shared_memory_lidar_scans_match_inline(sim_server):
    import asyncio
    import tempo_sim.TempoSensors.Lidar_pb2 as Lidar

    spawned = tw.spawn_actor(actor_type=SENSOR_RIG_TYPE)
    owner = spawned.name
    try:
        lidar = tw.add_component(component_type="TempoLidar", actor=owner, name="SharedMemoryLidar").name
        _check_shared_memory_against_inline(*asyncio.run(_shared_memory_against_inline(
            owner, lidar, ts.stream_lidar_scans, "StreamLidarScans",
            Lidar.LidarScanRequest(owner=owner, sensor=lidar))))
    finally:
        tw.destroy_actor(actor=owner)
