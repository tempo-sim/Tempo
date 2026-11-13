# Copyright Tempo Simulation, LLC. All Rights Reserved

"""End-to-end regression tests for the camera GPU->CPU readback path.

These exist to catch the failure modes that the readback optimizations could introduce and that a
unit test cannot see, because they live in the GPU copy / staging / fence machinery:

  * a frame whose pixels come from a different capture than its header describes (off by one),
  * a frame stitched together from two captures (tearing),
  * a dropped, duplicated or misplaced band of rows from the parallel copy and the parallel decode,
  * the four measurement types disagreeing with each other despite sharing one decode pass.

They actually render, so they need a GPU and TEMPO_SIM_RENDER=1 — same as test_sensors.py. They are
in the `sensors` group, which is intentionally NOT part of the default CI matrix.

Locally:  TEMPO_SIM_RENDER=1 Scripts/TestPythonAPI.sh sensors

## The test level

The tests load their own level, /TempoSensors/Maps/TempoSensorsTest, rather than using whatever map
the sim starts in. Both the level and the rig (BP_SensorRig) ship with this plugin, so nothing here
depends on the host project.

The level has to hold up its end, because several checks are only meaningful with the right
geometry in front of the camera:

  * A large flat wall perpendicular to +X, roughly 20 m down the axis from the origin and wide
    enough to fill the frame. Depth is measured along the camera axis, so a perpendicular wall
    reads the same value at every pixel — which is what makes a frame built from two captures
    unmissable in `test_no_tearing_within_a_frame`. Without it that test falls back to a weaker
    row-population check.
  * Something solid in front of the camera at all times, so `test_frame_matches_its_header` has a
    range to compare against. It skips, with instructions, if the camera sees only sky.
  * Two or three distinct labeled meshes between the camera and the wall, offset so they do not
    overlap in the image. These give `test_measurement_types_agree` bounding boxes to cross-check
    against the label image, and give the band-seam check some texture to work with.
  * Nothing moving, and no animated or flickering lights. The tests rely on the scene being static
    so that only the camera's motion changes the image.

Plugin content is only cooked when something references it, and nothing in a host project
references this level. TempoSensors/Config/Editor.ini lists it under [AlwaysCookMaps], which the
cooker honors on every cook. The alternatives do not work here: MapsToCook is ignored whenever maps
are passed on the command line, as Package.sh does in CI, and DirectoriesToAlwaysCook only scans
*.uasset files, never *.umap. If these tests skip saying the level is missing, that config not
making it into the package is the first thing to check.

## Environment

  TEMPO_TEST_LEVEL        Level to load. Default "/TempoSensors/Maps/TempoSensorsTest".
  TEMPO_TEST_RIG_ORIGIN   "x,y,z" in meters, Tempo right-handed frame. Default "0,0,1".
  TEMPO_TEST_RIG_TYPE     Actor type to spawn. Default "BP_SensorRig".
"""

import asyncio
import math
import operator
import os
import statistics
import struct
import time

import pytest

import tempo_sim.tempo_core as tc
import tempo_sim.tempo_sensors as ts
import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry
import tempo_sim.TempoCore.Time_pb2 as Time
import tempo_sim.TempoSensors.Sensors_pb2 as SensorsPb
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

pytestmark = pytest.mark.sensors

LEVEL = os.environ.get("TEMPO_TEST_LEVEL", "/TempoSensors/Maps/TempoSensorsTest")
RIG_TYPE = os.environ.get("TEMPO_TEST_RIG_TYPE", "BP_SensorRig")
RIG_ORIGIN = tuple(float(v) for v in os.environ.get("TEMPO_TEST_RIG_ORIGIN", "0,0,1").split(","))

# How many frames each test inspects. Enough to see a systematic off-by-one, short enough that a
# rendering test stays tolerable to run by hand.
NUM_FRAMES = 8

# The camera is stepped along its view axis by these amounts (meters) between frames. They are
# deliberately NOT uniform: with a constant step, a frame that is one capture behind still shows a
# plausible depth, whereas an irregular sequence pins each frame to exactly one pose.
STEP_DELTAS_M = [0.0, 0.9, 0.3, 1.7, 0.2, 1.1, 2.3, 0.5, 1.3]

# Depth is quantized logarithmically over [MinDepth, MaxDepth], so absolute precision degrades with
# range. A whole-frame confusion moves depth by a STEP_DELTAS_M entry (>= 0.2 m), so this is far
# tighter than needed to catch one while tolerating quantization.
DEPTH_TOL_M = 0.05

# Ray length for the ground-truth raycast, in meters.
MAX_RAY_M = 500.0

# How long to wait for the test level to come up. A level swap tears the gRPC services down and
# back up, so the first calls after it can fail before they start answering.
LEVEL_LOAD_TIMEOUT_S = 120.0
LEVEL_WARM_UP_S = 1.0


def _vec(x, y, z):
    return Geometry.Vector(x=x, y=y, z=z)


def _depths(depth_image):
    """Unpack the packed little-endian float32 blob into a list of floats."""
    count = depth_image.width_px * depth_image.height_px
    return list(struct.unpack_from(f"<{count}f", depth_image.depths_m))


def _center_depth(depth_image):
    w, h = depth_image.width_px, depth_image.height_px
    offset = ((h // 2) * w + (w // 2)) * 4
    return struct.unpack_from("<f", depth_image.depths_m, offset)[0]


def _forward_from_rotation(rotation):
    """Unit forward vector for a Tempo right-handed rotation (X forward, Y left, Z up)."""
    cp = math.cos(rotation.p)
    return (cp * math.cos(rotation.y), cp * math.sin(rotation.y), math.sin(rotation.p))


def _center_raycast(owner, header):
    """Raycast along the camera axis from the pose a measurement header says it was captured at."""
    loc = header.capture_transform.location
    fwd = _forward_from_rotation(header.capture_transform.rotation)
    return tw.raycast(
        start=_vec(loc.x, loc.y, loc.z),
        end=_vec(loc.x + fwd[0] * MAX_RAY_M, loc.y + fwd[1] * MAX_RAY_M, loc.z + fwd[2] * MAX_RAY_M),
        ignored_actors=[owner],
    )


def _center_truth(owner, header):
    """True distance along the camera axis from the header's pose, or None if the ray hits nothing."""
    hit = _center_raycast(owner, header)
    return hit.distance_m if hit.hit else None


async def _capture_frames(owner, sensor, num_frames, move_camera=True):
    """Move the camera between fixed steps and collect matching color/label/depth/box frames.

    Everything runs on one event loop on purpose. The synchronous stream wrappers drive each
    __anext__ through its own asyncio.run(), which lands on a new loop every time and makes
    TempoContext rebuild the channel underneath the stream — fine for the first frame, fatal for
    the second. Awaiting the async API keeps one channel for the whole session.
    """
    color_stream = ts.stream_color_images(owner=owner, sensor=sensor).__aiter__()
    label_stream = ts.stream_label_images(owner=owner, sensor=sensor).__aiter__()
    depth_stream = ts.stream_depth_images(owner=owner, sensor=sensor).__aiter__()
    box_stream = ts.stream_bounding_boxes(owner=owner, sensor=sensor).__aiter__()

    streams = (color_stream, label_stream, depth_stream, box_stream)
    frames = []
    pending = []
    x = RIG_ORIGIN[0]
    try:
        for index in range(num_frames):
            if move_camera:
                x += STEP_DELTAS_M[index % len(STEP_DELTAS_M)]
                await tw.set_actor_location(actor=owner, location=_vec(x, RIG_ORIGIN[1], RIG_ORIGIN[2]))

            # Have the next receive outstanding before stepping: a capture only happens when a request
            # is already pending, so issuing them after the step would race the capture.
            pending = [asyncio.ensure_future(stream.__anext__()) for stream in streams]
            await asyncio.sleep(0.05)

            await tc.step()

            color, label, depth, boxes = await asyncio.wait_for(asyncio.gather(*pending), timeout=60.0)
            frames.append({"color": color, "label": label, "depth": depth, "boxes": boxes})
    finally:
        # End the RPCs while this loop still runs. Left open, they outlive it (asyncio.run's loop
        # leaves its channel behind unclosed), and the next test's step waits for good on a write to
        # a stream nobody reads (see _close).
        for stream, future in zip(streams, pending or [None] * len(streams)):
            await _close(stream, future)

    return frames


def _load_level_and_wait():
    """Load the test level and return once the sim reports it as the current one."""
    try:
        tc.load_level(level=LEVEL)
    except Exception:
        # The level transition tears the gRPC services down and back up, which can strand this
        # call's continuation and surface as UNAVAILABLE / "Service is not active". The load still
        # happens, so confirm by polling below rather than failing on the call itself.
        pass

    expected = LEVEL.rsplit("/", 1)[-1]
    deadline = time.monotonic() + LEVEL_LOAD_TIMEOUT_S
    last = None
    while time.monotonic() < deadline:
        try:
            last = tc.get_current_level_name().level
            if last == expected:
                break
        except Exception as error:  # services are briefly down across the swap
            last = repr(error)
        time.sleep(0.5)
    else:
        pytest.fail(
            f"{LEVEL} did not come up within {LEVEL_LOAD_TIMEOUT_S}s; the sim last reported {last}"
        )


@pytest.fixture(scope="session")
def readback_level(sim_server):
    """Load the plugin's test level once for the whole session.

    Session-scoped deliberately. Loading a level is destructive — it tears the world down, destroys
    every actor and resets the clock — so doing it per test fought the function-scoped fixed_step
    fixture and produced "Step was aborted by an external pause or unpause". Session scope also
    fixes the ordering: pytest sets higher-scoped fixtures up first, so the level is guaranteed to
    be in place before fixed_step pauses and primes the clock.
    """
    available = tc.get_available_levels(search_path="/TempoSensors").levels
    if LEVEL not in available:
        pytest.skip(
            f"{LEVEL} is not in this build. Levels found under /TempoSensors: "
            f"{list(available) or 'none'}. Plugin content is only cooked when something references "
            "it — check that TempoSensors/Config/Editor.ini contributes "
            "[AlwaysCookMaps] +Map=/TempoSensors/Maps/TempoSensorsTest and repackage."
        )

    _load_level_and_wait()

    # A level's first frames can render before it is ready to (sky capture, shader and mesh setup), so
    # a capture a few frames after the load can come back without the scene in it. Let it run for a
    # moment first. Nothing else gives it that time: a stepping client gets one frame per step.
    tc.set_time_mode(time_mode=Time.TM_WALL_CLOCK)
    tc.play()
    time.sleep(LEVEL_WARM_UP_S)

    # Left loaded on purpose. Reloading on teardown would cost another transition, and the tests
    # that follow in this session are better off on a level that has a sensor rig in it.
    return LEVEL


@pytest.fixture(autouse=True)
def leave_sim_running():
    """Hand the sim back running after every test in this module.

    fixed_step pauses the clock and nothing else unpauses it, so anything later in the session that
    waits on a rendered frame — test_sensors.py's streaming test, say — blocks forever on a sim
    that will never tick. This has to be per-test rather than per-session: a session-scoped teardown
    runs after the whole session, which is far too late to help the tests that follow this file.

    autouse with no dependencies means it is set up before the fixtures each test requests, so it
    tears down after them and gets the last word on the sim's state.
    """
    yield
    try:
        tc.set_time_mode(time_mode=Time.TM_WALL_CLOCK)
        tc.play()
    except Exception as error:  # teardown only; the results are already collected
        print(f"Warning: could not restore the sim to a running state: {error!r}")


@pytest.fixture(scope="session")
def labeled_scene(readback_level):
    """Give every static mesh type in the level a distinct semantic ID.

    The bounding box cross-check needs labeled geometry in view, but which actors carry a label is
    normally decided by the project's semantic label table — exactly the kind of host-project
    dependency this file is trying to avoid. Assigning IDs over the API instead means the check
    works on any level with meshes in it, whatever the table says.
    """
    meshes = [m for m in ts.get_all_static_mesh_types().mesh_types if m.instance_count > 0]
    if not meshes:
        # This is a generator fixture, so it has to yield even with nothing to assign; returning
        # would be reported as a fixture error rather than reaching the skip in the test.
        yield []
        return

    # Label IDs must stay within what the camera's alpha encoding can represent
    # (GTempoCamera_Max_Label = 251); 0 means "unlabeled", so start at 1.
    assigned = []
    for index, mesh in enumerate(meshes[:251]):
        ts.set_static_mesh_type_semantic_id(static_mesh_path=mesh.mesh_path, semantic_id=index + 1)
        assigned.append(mesh.mesh_path)

    yield assigned

    for path in assigned:
        try:
            ts.set_static_mesh_type_semantic_id(static_mesh_path=path, semantic_id=-1)
        except Exception:
            pass  # teardown only; cannot affect results already collected


@pytest.fixture
def camera_rig(readback_level, fixed_step):
    """Yield (owner, sensor) for a camera parked at the test origin with identity rotation.

    Identity rotation keeps the camera's forward axis along +X, which is what lets the tests move
    the camera along its own view axis without reasoning about rotation conventions.

    Prefer a camera the level already provides: the test level ships with a sensor rig in it, and
    spawning another gave two cameras and dropped the new one into existing geometry. Only spawn
    when the level has none. Either way the camera is repositioned at the start of every test, so
    one test's motion cannot carry into the next.
    """
    def find_camera():
        return next(
            (
                sensor
                for sensor in ts.get_available_sensors().available_sensors
                if SensorsPb.MT_COLOR_IMAGE in sensor.measurement_types
            ),
            None,
        )

    cam = find_camera()
    spawned_owner = None
    if cam is None:
        spawned = tw.spawn_actor(
            actor_type=RIG_TYPE,
            transform=Geometry.Transform(
                location=_vec(*RIG_ORIGIN), rotation=Geometry.Rotation(r=0.0, p=0.0, y=0.0)
            ),
        )
        spawned_owner = spawned.name
        assert spawned_owner, f"could not spawn {RIG_TYPE}"
        cam = find_camera()
    if cam is None:
        pytest.skip(
            f"No camera found on {LEVEL} and none could be spawned from {RIG_TYPE}. The level "
            "should contain a sensor rig."
        )

    tw.set_actor_transform(
        actor=cam.owner,
        transform=Geometry.Transform(
            location=_vec(*RIG_ORIGIN), rotation=Geometry.Rotation(r=0.0, p=0.0, y=0.0)
        ),
    )

    try:
        yield cam.owner, cam.name
    finally:
        if spawned_owner:
            tw.destroy_actor(actor=spawned_owner)


def _pipelined(enabled):
    ts.set_pipelined_rendering_enabled(enabled=enabled)


@pytest.fixture(params=[False, True], ids=["non_pipelined", "pipelined"])
def pipelining(request):
    """Run each test both ways.

    Non-pipelined is the blocking path, where the game thread waits on the readback. Pipelined
    allows several captures in flight at once, which is where a read that resolves against the
    wrong staging contents shows up.
    """
    _pipelined(request.param)
    yield request.param
    _pipelined(False)


def test_frame_matches_its_header(camera_rig, pipelining):
    """Every frame's pixels must describe the pose its own header claims.

    The header carries capture_transform, the sensor's world pose at capture time. Raycasting from
    that pose gives the true distance to whatever the camera was looking at. The center pixel's
    depth is measured along the camera axis, so for the center ray the two are the same quantity
    and must agree. A frame delivered with someone else's pixels disagrees by a full step.
    """
    owner, sensor = camera_rig
    frames = asyncio.run(_capture_frames(owner, sensor, NUM_FRAMES))

    first = frames[0]["depth"].header
    rot = first.capture_transform.rotation
    if abs(rot.p) > 1e-3 or abs(rot.y) > 1e-3:
        pytest.skip(
            f"Camera is not axis-aligned (pitch={rot.p:.4f}, yaw={rot.y:.4f}); this test moves the "
            "rig along +X and assumes the camera looks that way. Set TEMPO_TEST_RIG_TYPE to a rig "
            "whose camera faces the actor's forward axis."
        )

    # Whether the scene suits the test is decided by the raycast alone. Judging frame 0's pixels
    # here would let the very failure this test exists to catch, pixels one capture behind their
    # header, pass itself off as an unsuitable scene and skip.
    if _center_truth(owner, first) is None:
        pytest.skip(
            "Nothing in front of the camera to range against — the raycast missed. Point "
            "TEMPO_TEST_RIG_ORIGIN at a spot facing a wall, or build the level described in this "
            "file's docstring."
        )

    observed = []
    for index, frame in enumerate(frames):
        header = frame["depth"].header
        truth = _center_raycast(owner, header)
        assert truth.hit, f"frame {index}: raycast from the header pose hit nothing"

        measured = _center_depth(frame["depth"])
        assert abs(measured - truth.distance_m) <= DEPTH_TOL_M, (
            f"frame {index} (sequence_id={header.sequence_id}): center depth {measured:.3f} m but "
            f"the header's own pose ranges {truth.distance_m:.3f} m to {truth.actor}. The pixels "
            f"belong to a different capture than the header describes."
        )
        observed.append(measured)

    # Guard against a vacuous pass: if the camera never actually moved, every frame is identical
    # and none of the above could have failed.
    assert max(observed) - min(observed) > 0.5, (
        f"depths barely varied across frames (range {max(observed) - min(observed):.3f} m), so this "
        "test could not have detected a stale frame. Did the camera actually move?"
    )


def test_no_tearing_within_a_frame(camera_rig, pipelining):
    """No frame may be stitched together from two different captures.

    Depth is measured along the camera axis, so a flat wall perpendicular to that axis reads the
    same value at every pixel, and everything in front of it reads less. Rows left over from the
    previous capture carry that capture's wall distance, which is farther than anything this
    capture can legitimately contain, so counting pixels at that distance catches a tear without
    caring how much foreground the camera's motion brings into view. Where the scene has no such
    wall, fall back to checking that the frame is not a blend of its neighbours.
    """
    owner, sensor = camera_rig
    frames = asyncio.run(_capture_frames(owner, sensor, NUM_FRAMES))

    depths = [_depths(frame["depth"]) for frame in frames]
    if not any(math.isfinite(d) for d in depths[0]):
        pytest.skip("First frame had no finite depths; nothing to check.")

    # The wall distance comes from the header pose, not from the pixels, so a torn frame cannot
    # vote on its own reference. The scene qualifies for the strong form if a wall dominates any
    # frame; judging only the first would let a tear there hide the whole check.
    walls = [_center_truth(owner, frame["depth"].header) for frame in frames]
    wall_fractions = [
        sum(1 for d in frame_depths if abs(d - wall) <= DEPTH_TOL_M) / len(frame_depths)
        if wall is not None else 0.0
        for frame_depths, wall in zip(depths, walls)
    ]
    if max(wall_fractions) > 0.5:
        # Strong form. Frame k may hold pixels at frame k-1's wall distance only if the camera did
        # not move enough between them for the two to be told apart.
        for index in range(1, len(frames)):
            prev_wall, wall = walls[index - 1], walls[index]
            if prev_wall is None or wall is None or abs(prev_wall - wall) <= 2 * DEPTH_TOL_M:
                continue
            stale = sum(1 for d in depths[index] if abs(d - prev_wall) <= DEPTH_TOL_M)
            assert stale / len(depths[index]) <= 0.01, (
                f"frame {index}: {stale} of {len(depths[index])} pixels sit at {prev_wall:.3f} m, "
                f"the previous capture's wall distance, while this capture's wall is at "
                f"{wall:.3f} m. Nothing in this capture can be that far away, so those rows came "
                "from the previous capture."
            )
        return

    # Weak form: no flat wall available. A torn frame is part previous capture and part current,
    # split by a horizontal boundary, so its rows fall into two groups — those still identical to
    # the previous frame and those that changed. A clean frame has one group or the other, not both
    # in quantity. Subsample within each row; a tear covers whole rows, so sampling loses nothing.
    stride = 8
    for index in range(1, len(frames)):
        prev, cur = depths[index - 1], depths[index]
        if len(prev) != len(cur):
            continue
        w, h = frames[index]["depth"].width_px, frames[index]["depth"].height_px
        same_rows, changed_rows = 0, 0
        for y in range(h):
            base = y * w
            sampled = range(base, base + w, stride)
            same = sum(1 for i in sampled if abs(cur[i] - prev[i]) <= DEPTH_TOL_M)
            fraction = same / len(sampled)
            if fraction > 0.99:
                same_rows += 1
            elif fraction < 0.01:
                changed_rows += 1
        assert not (same_rows > h * 0.1 and changed_rows > h * 0.1), (
            f"frame {index}: {same_rows} rows are identical to the previous frame while "
            f"{changed_rows} rows changed completely. The image is split between two captures."
        )


def test_no_band_seam_artifacts(camera_rig, pipelining):
    """No horizontal artifacts at the row-band boundaries the parallel copy and decode use.

    Both the staging-surface copy and the measurement decode split the image into bands of rows and
    process them concurrently. A boundary computed wrongly drops, duplicates or misplaces the rows
    at a band edge, which lands at multiples of Height/32 and Height/64 and nowhere else (small
    images use fewer bands, always a power of two, so their seams are among the same rows).
    Comparing row-to-row change at those rows against the rest of the image turns that into a
    signal that does not depend on scene content.
    """
    owner, sensor = camera_rig
    frames = asyncio.run(_capture_frames(owner, sensor, NUM_FRAMES))

    for index, frame in enumerate(frames):
        # Use the color image: it carries content in any scene, whereas the label image is all
        # zeros unless labeled actors happen to be in shot, which would make this vacuous.
        color = frame["color"]
        w, h = color.width_px, color.height_px
        if h < 128:
            pytest.skip(f"Image is only {h} rows tall; too short to separate band seams from noise.")
        data = color.data
        row_bytes = w * 3

        def row_delta(y):
            """Mean absolute difference between row y and the one above it."""
            a = data[(y - 1) * row_bytes: y * row_bytes]
            b = data[y * row_bytes: (y + 1) * row_bytes]
            return sum(map(abs, map(operator.sub, a, b))) / row_bytes

        # Every seam row, and every eighth other row for the baseline. The baseline is a
        # percentile, so a sample serves it, and the whole image would be minutes of pure-Python
        # byte arithmetic per run.
        seam_rows = set()
        for bands in (32, 64):
            for band in range(1, bands):
                row = (h * band) // bands
                if 1 <= row < h:
                    seam_rows.add(row)

        seam = [row_delta(y) for y in sorted(seam_rows)]
        rest = [row_delta(y) for y in range(1, h, 8) if y not in seam_rows]
        if not seam or not rest:
            continue

        # A band bug makes seam rows discontinuous relative to ordinary rows. Compare against the
        # bulk of the distribution rather than the mean, which the artifact itself would inflate.
        rest_sorted = sorted(rest)
        p99 = rest_sorted[min(len(rest_sorted) - 1, int(len(rest_sorted) * 0.99))]
        worst = max(seam)

        if p99 < 0.5:
            pytest.skip(
                "The image is too featureless for a seam to stand out from ordinary row-to-row "
                "change; point the camera at something with texture."
            )
        assert worst <= max(p99 * 3.0, 2.0), (
            f"frame {index}: row-to-row label change at a band seam is {worst:.2f}, against a 99th "
            f"percentile of {p99:.2f} for ordinary rows. Rows are being dropped, duplicated or "
            "misplaced at a band boundary."
        )


def _boxes_from_label_image(label):
    """The bounding box of every instance id in a label image, as {id: [x0, y0, x1, y1]}.

    Row by row, with bytes.index/rindex finding the extents at C speed; a per-pixel Python loop over
    a full frame takes seconds.
    """
    w, h = label.width_px, label.height_px
    data = label.data
    expected = {}
    for y in range(h):
        row = data[y * w:(y + 1) * w]
        for instance_id in set(row):
            if instance_id == 0:
                continue
            x0, x1 = row.index(instance_id), row.rindex(instance_id)
            box = expected.get(instance_id)
            if box is None:
                expected[instance_id] = [x0, y, x1, y]
            else:
                box[0] = min(box[0], x0)
                box[2] = max(box[2], x1)
                box[3] = y
    return expected


def test_measurement_types_agree(camera_rig, labeled_scene, pipelining):
    """Color, label, depth and bounding boxes for one capture must be mutually consistent.

    All four are produced by a single pass over one pixel image, so they are supposed to be four
    views of the same frame. Recomputing the bounding boxes client-side from the label image and
    requiring an exact match is a whole-image cross-check of the label output against the box
    output — if the fused pass drops or misplaces rows in either, the boxes stop matching.
    """
    owner, sensor = camera_rig
    frames = asyncio.run(_capture_frames(owner, sensor, NUM_FRAMES))

    checked_boxes = 0
    for index, frame in enumerate(frames):
        headers = {kind: frame[kind].header for kind in ("color", "label", "depth", "boxes")}
        sequence_ids = {kind: h.sequence_id for kind, h in headers.items()}
        assert len(set(sequence_ids.values())) == 1, (
            f"frame {index}: measurement types disagree on sequence_id: {sequence_ids}"
        )
        capture_times = {kind: h.capture_time_s for kind, h in headers.items()}
        assert len(set(capture_times.values())) == 1, (
            f"frame {index}: measurement types disagree on capture_time_s: {capture_times}"
        )

        label, boxes = frame["label"], frame["boxes"]
        assert (label.width_px, label.height_px) == (boxes.width_px, boxes.height_px)
        assert (label.width_px, label.height_px) == (frame["depth"].width_px, frame["depth"].height_px)
        assert (label.width_px, label.height_px) == (frame["color"].width_px, frame["color"].height_px)
        assert len(label.data) == label.width_px * label.height_px
        assert len(frame["color"].data) == label.width_px * label.height_px * 3
        assert len(frame["depth"].depths_m) == label.width_px * label.height_px * 4

        expected = _boxes_from_label_image(label)

        actual = {
            b.instance_id: [b.min_x_px, b.min_y_px, b.max_x_px, b.max_y_px]
            for b in boxes.bounding_boxes
        }
        assert actual == expected, (
            f"frame {index}: bounding boxes disagree with the label image they were computed from. "
            f"Only in the response: {set(actual) - set(expected)}; only in the label image: "
            f"{set(expected) - set(actual)}; differing extents: "
            f"{ {k: (actual[k], expected[k]) for k in set(actual) & set(expected) if actual[k] != expected[k]} }"
        )
        checked_boxes += len(expected)

    if checked_boxes == 0:
        pytest.skip(
            "No labeled objects were in view, so the bounding box cross-check had nothing to "
            "compare. Place a few labeled meshes in front of the camera (see this file's docstring)."
        )


def test_sequence_ids_are_continuous(camera_rig, pipelining):
    """Sequence ids and capture times must advance by exactly one fixed step per capture.

    A dropped or repeated capture would show up here, and this is the one check that needs nothing
    of the scene at all.
    """
    owner, sensor = camera_rig
    frames = asyncio.run(_capture_frames(owner, sensor, NUM_FRAMES, move_camera=False))

    headers = [frame["depth"].header for frame in frames]
    ids = [h.sequence_id for h in headers]
    assert ids == sorted(ids), f"sequence ids went backwards: {ids}"
    assert len(set(ids)) == len(ids), f"a capture was delivered twice: {ids}"

    gaps = [b - a for a, b in zip(ids, ids[1:])]
    assert all(g == gaps[0] for g in gaps), f"sequence id gaps were not uniform: {gaps} (ids {ids})"

    times = [h.capture_time_s for h in headers]
    deltas = [b - a for a, b in zip(times, times[1:])]
    if len(deltas) > 1:
        # Loose enough for float sim-time accumulation, tight enough that a dropped or repeated
        # capture (which shifts one delta by a whole step) is unmissable.
        assert statistics.pstdev(deltas) < 1e-4, (
            f"capture times did not advance uniformly in fixed-step mode: {deltas}"
        )



STEPS_PER_GAME_FRAME = 20


async def _steps_until_frame_arrives(owner, sensor, steps_per_game_frame):
    """Capture a frame in one game frame, then advance the next one step at a time, pausing between
    steps so the GPU can finish. Returns how many steps ran before the frame arrived, or None if it
    was still outstanding when only the step that completes the game frame was left."""
    stream = ts.stream_color_images(owner=owner, sensor=sensor).__aiter__()
    pending = asyncio.ensure_future(stream.__anext__())
    try:
        await asyncio.sleep(0.05)
        await tc.advance_steps(steps=steps_per_game_frame)

        for steps in range(steps_per_game_frame):
            if pending.done():
                pending.result()
                return steps
            if steps == steps_per_game_frame - 1:
                break
            await tc.step()
            await asyncio.sleep(0.02)
        return None
    finally:
        # Close the stream, or the camera keeps its color request pending into later tests.
        await _close(stream, pending)


def test_a_pipelined_frame_arrives_between_game_frames(camera_rig):
    """A pipelined frame is read back after its own frame ends. While a client advances the next game
    frame one step at a time, no frame ends, so the readback has to be noticed between steps: by
    polling the frame's fence, and by getting the RHI to process the GPU's completed work, which
    some RHIs only do when something submits."""
    owner, sensor = camera_rig
    _pipelined(True)
    tc.set_steps_per_game_frame(steps_per_game_frame=STEPS_PER_GAME_FRAME)
    try:
        arrived_after = asyncio.run(_steps_until_frame_arrives(owner, sensor, STEPS_PER_GAME_FRAME))
    finally:
        tc.set_steps_per_game_frame(steps_per_game_frame=1)
        _pipelined(False)

    assert arrived_after is not None, (
        f"the frame was still outstanding after {STEPS_PER_GAME_FRAME - 1} steps "
        f"({(STEPS_PER_GAME_FRAME - 1) * 20} ms of wall clock), so it would only have arrived with the "
        "next game frame"
    )


async def _capture_each_game_frame(owner, sensor, steps_per_game_frame, game_frames):
    """Advance one game frame at a time with a depth request pending, and collect what each yields:
    the image, or None if no capture came within a few seconds.

    Depth, so the camera's depth stays on: turning it on or off reconfigures the camera, which skips
    captures until the reconfigure has applied. A test before this one may have left it on, and with
    color alone the camera would turn it off part-way through."""
    stream = ts.stream_depth_images(owner=owner, sensor=sensor).__aiter__()
    images = []
    pending = None
    try:
        for _ in range(game_frames):
            if pending is None:
                pending = asyncio.ensure_future(stream.__anext__())
            await asyncio.sleep(0.05)
            await tc.advance_steps(steps=steps_per_game_frame)
            # asyncio.wait, not wait_for: a missed capture must leave the read pending for the next
            # game frame, and cancelling it would end the stream.
            done, _ = await asyncio.wait({pending}, timeout=5.0)
            if done:
                images.append(pending.result())
                pending = None
            else:
                images.append(None)
    finally:
        await _close(stream, pending)
    return images


def test_a_sensor_at_the_game_frame_rate_captures_every_game_frame(camera_rig, fixed_step):
    """A sensor whose rate equals the game frame rate must capture in every game frame.

    The timer manager fires a timer only once it is strictly overdue. A capture timer restarted on a
    frame boundary, as a rate change restarts it, is due exactly on every later frame, so without
    allowance it fires every other frame and halves the capture rate."""
    owner, sensor = camera_rig
    steps_per_game_frame = 2
    rate_hz = fixed_step / steps_per_game_frame
    original_rate = tw.get_property(actor=owner, component=sensor, property="RateHz").float_value
    tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=rate_hz))
    tc.set_steps_per_game_frame(steps_per_game_frame=steps_per_game_frame)
    try:
        # One stream throughout, so the camera's depth stays requested. Its first game frame runs the old
        # timer, whose callback re-arms it at the new rate, and absorbs the reconfigure if depth was off;
        # a second stream would leave a gap long enough for the camera to turn depth off again.
        images = asyncio.run(_capture_each_game_frame(owner, sensor, steps_per_game_frame, 7))[1:]
    finally:
        tc.set_steps_per_game_frame(steps_per_game_frame=1)
        tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=original_rate))

    missed = [index for index, image in enumerate(images) if image is None]
    assert not missed, f"no capture in game frames {missed} of {len(images)}, at {rate_hz} Hz with one game frame per period"



async def _capture_times(owner, sensor, captures, max_steps_per_capture):
    """Step one step at a time with a depth request pending, and collect the capture times of the
    first `captures` captures. A capture that hasn't come within `max_steps_per_capture` steps is
    recorded as None."""
    stream = ts.stream_depth_images(owner=owner, sensor=sensor).__aiter__()
    times = []
    pending = None
    try:
        for _ in range(captures):
            pending = asyncio.ensure_future(stream.__anext__())
            await asyncio.sleep(0.05)
            for _ in range(max_steps_per_capture):
                await tc.step()
                done, _ = await asyncio.wait({pending}, timeout=1.0)
                if done:
                    break
            if pending.done():
                times.append(pending.result().header.capture_time_s)
                pending = None
            else:
                times.append(None)
                break
    finally:
        await _close(stream, pending)
    return times


def test_a_slower_sensor_captures_on_multiples_of_its_period(camera_rig, fixed_step):
    """In FixedStep a sensor's captures fall on multiples of its period in sim time.

    A capture timer's phase would otherwise come from whenever it happened to start: a level load in
    WallClock, a rate change, a switch of time mode. A sensor slower than the frame rate would then
    capture on different steps from run to run, and two sensors at one rate on alternate ones."""
    owner, sensor = camera_rig
    rate_hz = fixed_step / 2.0
    period = 1.0 / rate_hz
    original_rate = tw.get_property(actor=owner, component=sensor, property="RateHz").float_value
    tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=rate_hz))
    try:
        # One stream throughout, so the camera's depth stays requested; a second stream would leave a
        # gap long enough for the camera to turn depth off again, delaying a capture by a step. Its
        # first two captures absorb the rate change: the timer's next fire is on the old cadence and
        # may have been armed before the change, so it re-arms one old period on, and only that fire's
        # re-arm aligns to the new period. Either can also absorb a depth reconfigure.
        all_times = asyncio.run(_capture_times(owner, sensor, 7, 4))
        assert None not in all_times, f"a capture didn't come within 4 steps: {all_times}"
        times = all_times[2:]
    finally:
        tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=original_rate))

    assert None not in times, f"a capture didn't come within 4 steps: {times}"
    off_grid = [t for t in times if abs(t / period - round(t / period)) > 1e-6]
    assert not off_grid, f"captures at {times} s are not on multiples of the {period} s period (first two, dropped: {all_times[:2]})"
    assert all(abs((b - a) - period) < 1e-6 for a, b in zip(times, times[1:])), f"captures at {times} s are not one period apart"


async def _sequence_ids_while_stepping(owner, sensor, steps):
    """Read a depth stream continuously while stepping back to back, without waiting on the stream
    between steps, and return the sequence ids it delivered."""
    stream = ts.stream_depth_images(owner=owner, sensor=sensor).__aiter__()
    ids = []

    async def consume():
        async for image in stream:
            ids.append(image.header.sequence_id)

    consumer = asyncio.ensure_future(consume())
    try:
        await asyncio.sleep(0.05)
        for _ in range(steps):
            await tc.step()
        # The last captures' readbacks finish in the frames after they were taken.
        for _ in range(3):
            await asyncio.sleep(0.05)
            await tc.step()
    finally:
        await _close(stream, consumer)
    return ids


@pytest.mark.parametrize("steps_per_game_frame", [1, 4])
def test_a_stream_read_while_stepping_drops_no_capture(camera_rig, fixed_step, steps_per_game_frame):
    """A stream read while the client steps without waiting on it must receive every capture.

    With pipelined rendering a capture can be taken on a request that an earlier capture's readback
    then answers, so its own readback has no request until the stream asks again, which it does only
    once its last response is written. Sending that readback before then drops the capture."""
    owner, sensor = camera_rig
    rate_hz = fixed_step / steps_per_game_frame
    original_rate = tw.get_property(actor=owner, component=sensor, property="RateHz").float_value
    tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=rate_hz))
    tc.set_steps_per_game_frame(steps_per_game_frame=steps_per_game_frame)
    _pipelined(True)
    try:
        ids = asyncio.run(_sequence_ids_while_stepping(owner, sensor, 20 * steps_per_game_frame))
    finally:
        _pipelined(False)
        tc.set_steps_per_game_frame(steps_per_game_frame=1)
        tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=original_rate))

    # The first captures absorb the rate change and a depth reconfigure.
    ids = ids[2:]
    assert len(ids) >= 10, f"only {len(ids)} captures arrived: {ids}"
    gaps = [b - a for a, b in zip(ids, ids[1:])]
    dropped = [(a, b) for a, b in zip(ids, ids[1:]) if b - a != 1]
    assert not dropped, f"captures were dropped between sequence ids {dropped} (all ids {ids}, gaps {gaps})"


@pytest.mark.parametrize("steps_per_game_frame", [1, 4])
def test_a_stream_read_across_paused_frames_drops_no_capture(camera_rig, fixed_step, steps_per_game_frame):
    """A stream read while the client's steps keep missing the between-frames wait must receive
    every capture.

    When the wait for the next Step times out, the frame runs paused, and its measurement send comes
    before its server tick. A readback sent during the wait whose write is still in flight leaves
    the next readback with no request at that send. A one-millisecond wait makes almost every step
    miss it."""
    owner, sensor = camera_rig
    rate_hz = fixed_step / steps_per_game_frame
    original_rate = tw.get_property(actor=owner, component=sensor, property="RateHz").float_value
    tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=rate_hz))
    tc.set_steps_per_game_frame(steps_per_game_frame=steps_per_game_frame)
    tc.exec_console_command(command="Tempo.StepWaitTimeoutMs 1")
    _pipelined(True)
    try:
        ids = asyncio.run(_sequence_ids_while_stepping(owner, sensor, 20 * steps_per_game_frame))
    finally:
        _pipelined(False)
        tc.exec_console_command(command="Tempo.StepWaitTimeoutMs 50")
        tc.set_steps_per_game_frame(steps_per_game_frame=1)
        tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=original_rate))

    # The first captures absorb the rate change and a depth reconfigure.
    ids = ids[2:]
    assert len(ids) >= 10, f"only {len(ids)} captures arrived: {ids}"
    dropped = [(a, b) for a, b in zip(ids, ids[1:]) if b - a != 1]
    assert not dropped, f"captures were dropped between sequence ids {dropped} (all ids {ids})"


# ------------------------------------------------------------------------------------------------
# Delivery under concurrency: several streams, a lidar's multi-segment sweeps, streams opened late,
# cancelled part-way and torn down with the level. What these guard is who launches a decode and
# when a response is written, which no single-stream test reaches.
# ------------------------------------------------------------------------------------------------

# Engine frames a camera goes on rendering labels after its last label or bounding box request, and
# depth after its last depth request. Stepping past it puts the camera in its color-only state.
DEBOUNCE_FRAMES = 30

SETTLE_STEPS = int(os.environ.get("TEMPO_TEST_SETTLE_STEPS", str(DEBOUNCE_FRAMES + 10)))

# Steps the soak test advances. The races it is after are statistical, so more is better; this is
# what stays tolerable on a developer's machine.
SOAK_STEPS = int(os.environ.get("TEMPO_TEST_SOAK_STEPS", "300"))


def _sensors_measuring(measurement_type):
    return [
        (sensor.owner, sensor.name)
        for sensor in ts.get_available_sensors().available_sensors
        if measurement_type in sensor.measurement_types
    ]


class _Rates:
    """Sets every listed sensor's RateHz, and puts the originals back on exit."""

    def __init__(self, sensors, rate_hz):
        self.sensors = sensors
        self.rate_hz = rate_hz
        self.originals = {}

    def __enter__(self):
        for owner, sensor in self.sensors:
            self.originals[(owner, sensor)] = tw.get_property(actor=owner, component=sensor, property="RateHz").float_value
            tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=self.rate_hz))
        return self

    def __exit__(self, *exc):
        for (owner, sensor), rate_hz in self.originals.items():
            try:
                tw.set_property(actor=owner, component=sensor, property="RateHz", value=WorldControl.Value(float_value=rate_hz))
            except Exception:
                pass  # teardown only; the level may have been swapped under it
        return False


async def _close(stream, *tasks):
    """Cancel what is still reading a stream and end its RPC, so its request doesn't stay pending on
    the sensor into later tests, and the sim doesn't write the sensor's next response to a client
    that will never read it.

    The reads go first: an async generator can't be closed while one of them is running it. Closing
    it then cancels the call."""
    tasks = [task for task in tasks if task is not None]
    for task in tasks:
        if not task.done():
            task.cancel()
    for task in tasks:
        try:
            await task
        except (asyncio.CancelledError, Exception):
            pass
    try:
        await stream.aclose()
    except (RuntimeError, asyncio.CancelledError):
        pass


async def _headers_while_stepping(streams, steps, settle_steps=0):
    """Read every stream in `streams` ({name: async iterator}) continuously while stepping back to
    back, and return {name: [message, ...]} with only what a delivery check needs kept from each
    message: its header, and a lidar segment's place in its sweep.

    The caller opens the streams, and has to do it in the body of a coroutine: the client picks a
    wrapper's blocking or async variant by its caller's frame, so one opened from a helper, a lambda
    or a comprehension comes back as the blocking generator.

    What arrives during the first `settle_steps` steps is read and left out."""
    received = {name: [] for name in streams}

    async def consume(name, stream):
        async for message in stream:
            entry = {"header": message.header}
            if hasattr(message, "scan_count"):
                entry["scan_count"] = message.scan_count
                entry["azimuth_min"] = message.azimuth_range_rad.min
            received[name].append(entry)

    consumers = {name: asyncio.ensure_future(consume(name, stream)) for name, stream in streams.items()}
    try:
        await asyncio.sleep(0.1)
        for _ in range(settle_steps):
            await tc.step()
        settled = {name: len(messages) for name, messages in received.items()}
        for _ in range(steps):
            await tc.step()
            failed = [name for name, consumer in consumers.items() if consumer.done()]
            assert not failed, f"streams {failed} ended while stepping: {[consumers[n].exception() for n in failed]}"
        # The last captures' readbacks finish in the frames after they were taken.
        for _ in range(3):
            await asyncio.sleep(0.05)
            await tc.step()
        await asyncio.sleep(0.2)
    finally:
        for name, stream in streams.items():
            await _close(stream, consumers[name])
    return {name: messages[settled[name]:] for name, messages in received.items()}


def _assert_no_capture_dropped(name, ids, skip=2, at_least=10):
    """`ids` are the sequence ids a stream delivered, one per capture. The first `skip` absorb a rate
    change and a depth or color reconfigure."""
    ids = ids[skip:]
    assert len(ids) >= at_least, f"{name}: only {len(ids)} captures arrived: {ids}"
    out_of_order = [(a, b) for a, b in zip(ids, ids[1:]) if b <= a]
    assert not out_of_order, f"{name}: captures arrived out of order or twice at {out_of_order} (all ids {ids})"
    dropped = [(a, b) for a, b in zip(ids, ids[1:]) if b - a != 1]
    assert not dropped, f"{name}: captures were dropped between sequence ids {dropped} (all ids {ids})"


def _assert_transmitted_after_capture(name, headers):
    """A measurement goes on the wire no earlier than it was captured, and a stream's transmission
    times never go back."""
    # The sensors carry the transmission time in single precision, so it can read a hair under a
    # capture time it equals. What this is after is a time from an earlier step, a tenth of a second off.
    tolerance = 1e-3
    early = [
        (h.sequence_id, h.capture_time_s, h.transmission_time_s)
        for h in headers
        if h.transmission_time_s < h.capture_time_s - tolerance
    ]
    assert not early, f"{name}: (sequence id, capture time, transmission time) transmitted before capture: {early}"
    times = [h.transmission_time_s for h in headers]
    backwards = [(a, b) for a, b in zip(times, times[1:]) if b < a - tolerance]
    assert not backwards, f"{name}: transmission times went back at {backwards}"


def _sweeps(segments):
    """Group a lidar stream's segments by sweep, in arrival order: [(sequence_id, [segment, ...])]."""
    sweeps = []
    for segment in segments:
        sequence_id = segment["header"].sequence_id
        if sweeps and sweeps[-1][0] == sequence_id:
            sweeps[-1][1].append(segment)
        else:
            sweeps.append((sequence_id, [segment]))
    return sweeps


def _assert_sweeps_whole(name, segments, skip=2, at_least=10):
    """Every sweep a lidar stream delivered has each of its segments exactly once, the sweeps are
    consecutive, and no sweep's segments are interleaved with another's."""
    sweeps = _sweeps(segments)
    # The last sweep may have been cut off by the end of the test.
    sweeps = sweeps[skip:-1]
    assert len(sweeps) >= at_least, f"{name}: only {len(sweeps)} whole sweeps arrived"
    ids = [sequence_id for sequence_id, _ in sweeps]
    assert len(set(ids)) == len(ids), (
        f"{name}: a sweep's segments were split up by another sweep's, or a sweep was sent twice: {ids}"
    )
    for sequence_id, sweep in sweeps:
        scan_count = sweep[0]["scan_count"]
        azimuths = [round(segment["azimuth_min"], 6) for segment in sweep]
        assert len(sweep) == scan_count and len(set(azimuths)) == scan_count, (
            f"{name}: sweep {sequence_id} should have {scan_count} distinct segments and delivered "
            f"{len(sweep)}, starting at azimuths {azimuths}"
        )
    dropped = [(a, b) for a, b in zip(ids, ids[1:]) if b - a != 1]
    assert not dropped, f"{name}: sweeps were dropped between sequence ids {dropped} (all ids {ids})"


LIDAR_TYPE = os.environ.get("TEMPO_TEST_LIDAR_TYPE", "TempoLidar")


def _add_lidar():
    """Add a lidar to the actor that carries the level's camera and return (owner, sensor), or None
    if the level has no camera to put it beside or the lidar doesn't show up as a sensor."""
    cameras = _sensors_measuring(SensorsPb.MT_COLOR_IMAGE)
    if not cameras:
        return None
    owner = cameras[0][0]
    name = tw.add_component(component_type=LIDAR_TYPE, actor=owner, name="ReadbackTestLidar").name
    for _ in range(20):
        if (owner, name) in _sensors_measuring(SensorsPb.MT_LIDAR_SCAN):
            return owner, name
        time.sleep(0.1)
    tw.destroy_component(actor=owner, component=name)
    return None


@pytest.fixture
def lidar_rig(readback_level, fixed_step):
    """Yield (owner, sensor) for a lidar: one the test level provides, or else one added to its
    sensor rig for the test."""
    lidars = _sensors_measuring(SensorsPb.MT_LIDAR_SCAN)
    added = None
    if not lidars:
        added = _add_lidar()
        if added is None:
            pytest.skip(f"No lidar found on {LEVEL} and a {LIDAR_TYPE} could not be added to its sensor rig.")
    try:
        yield lidars[0] if lidars else added
    finally:
        if added is not None:
            try:
                tw.destroy_component(actor=added[0], component=added[1])
            except Exception:
                pass  # teardown only


@pytest.mark.parametrize("steps_per_game_frame", [1, 4])
def test_a_lidar_stream_read_while_stepping_delivers_whole_sweeps(lidar_rig, fixed_step, steps_per_game_frame):
    """A lidar stream read while the client steps must receive every sweep, each segment once.

    A sweep is several responses to one request. The stream asks for its next measurement whenever
    its write queue drains, which can happen part-way through a sweep while the decode is still
    responding; a lidar that takes that as a second request sends the next sweep to the stream
    twice, and one that loses it sends none."""
    owner, sensor = lidar_rig
    tc.set_steps_per_game_frame(steps_per_game_frame=steps_per_game_frame)
    _pipelined(True)
    async def run():
        return await _headers_while_stepping(
            {"lidar": ts.stream_lidar_scans(owner=owner, sensor=sensor).__aiter__()},
            20 * steps_per_game_frame)

    try:
        with _Rates([lidar_rig], fixed_step / steps_per_game_frame):
            received = asyncio.run(run())
    finally:
        _pipelined(False)
        tc.set_steps_per_game_frame(steps_per_game_frame=1)

    _assert_sweeps_whole("lidar", received["lidar"])
    _assert_transmitted_after_capture("lidar", [segment["header"] for segment in received["lidar"]])


@pytest.mark.parametrize("steps_per_game_frame", [1, 4])
def test_every_stream_read_at_once_while_stepping_drops_no_capture(lidar_rig, fixed_step, steps_per_game_frame):
    """Every camera's color and depth and every lidar, streamed at once over many steps, must each
    receive every capture, in order, with nothing transmitted before it was captured.

    This is the soak for what a single stream can't provoke: one sensor's decode responding while
    another's is launched, a read completing while the game thread is sending, two streams of one
    camera answered by one decode. Their failures are a capture missing from one stream, or
    delivered out of order, once in some hundreds of steps. TEMPO_TEST_SOAK_STEPS lengthens it."""
    cameras = _sensors_measuring(SensorsPb.MT_COLOR_IMAGE)
    lidars = _sensors_measuring(SensorsPb.MT_LIDAR_SCAN)
    if not cameras:
        pytest.skip(f"No camera found on {LEVEL}.")

    async def run():
        streams = {}
        for owner, sensor in cameras:
            streams[f"{owner}/{sensor} color"] = ts.stream_color_images(owner=owner, sensor=sensor).__aiter__()
            streams[f"{owner}/{sensor} depth"] = ts.stream_depth_images(owner=owner, sensor=sensor).__aiter__()
        for owner, sensor in lidars:
            streams[f"{owner}/{sensor} lidar"] = ts.stream_lidar_scans(owner=owner, sensor=sensor).__aiter__()
        return await _headers_while_stepping(streams, SOAK_STEPS, settle_steps=SETTLE_STEPS * steps_per_game_frame)

    tc.set_steps_per_game_frame(steps_per_game_frame=steps_per_game_frame)
    _pipelined(True)
    try:
        with _Rates(cameras + lidars, fixed_step / steps_per_game_frame):
            received = asyncio.run(run())
    finally:
        _pipelined(False)
        tc.set_steps_per_game_frame(steps_per_game_frame=1)

    expected = SOAK_STEPS // steps_per_game_frame
    for name, messages in received.items():
        headers = [message["header"] for message in messages]
        if name.endswith("lidar"):
            _assert_sweeps_whole(name, messages, at_least=expected // 2)
        else:
            _assert_no_capture_dropped(name, [h.sequence_id for h in headers], at_least=expected // 2)
        _assert_transmitted_after_capture(name, headers)


async def _step_reading(stream, steps):
    """Step `steps` times with a read of `stream` outstanding before each, and return the messages."""
    messages = []
    pending = None
    try:
        for _ in range(steps):
            pending = asyncio.ensure_future(stream.__anext__())
            await asyncio.sleep(0.05)
            await tc.step()
            messages.append(await asyncio.wait_for(pending, timeout=60.0))
            pending = None
    finally:
        if pending is not None:
            pending.cancel()
    return messages


async def _settle_reading(stream, steps):
    """Step at least `steps` times reading `stream`, without requiring a capture from every step, and
    return with no read outstanding.

    A camera that stops being asked for depth turns it off some frames later, and the reconfigure
    skips a capture. Whether that falls in a test depends on what ran before it, so a test that
    needs a capture per step first steps past it with this."""
    pending = None
    for step in range(steps + 10):
        if pending is None:
            if step >= steps:
                return
            pending = asyncio.ensure_future(stream.__anext__())
            await asyncio.sleep(0.05)
        await tc.step()
        done, _ = await asyncio.wait({pending}, timeout=2.0)
        if done:
            pending.result()
            pending = None
    pending.cancel()
    raise AssertionError(f"no capture arrived in the last steps of {steps + 10} while settling")


def test_a_label_stream_opened_late_is_served_promptly(camera_rig, labeled_scene, fixed_step):
    """Label and bounding box streams opened on a camera that has only streamed color for a while
    must get labeled frames within a couple of captures, and they must be real: the labels of the
    scene, agreeing with the boxes and with the color frame of the same capture.

    A camera with no label request has no reason to render what the labels are read from. One that
    stops must start again as soon as a request arrives, and must not answer a label request from a
    capture that was rendered without them, which would be a label image of zeros."""
    owner, sensor = camera_rig

    async def run():
        color_stream = ts.stream_color_images(owner=owner, sensor=sensor).__aiter__()
        label_stream = box_stream = None
        pending = []
        try:
            # Long enough for the camera to have settled into rendering color alone.
            await _settle_reading(color_stream, DEBOUNCE_FRAMES + 10)
            before = await _step_reading(color_stream, 5)

            label_stream = ts.stream_label_images(owner=owner, sensor=sensor).__aiter__()
            box_stream = ts.stream_bounding_boxes(owner=owner, sensor=sensor).__aiter__()
            pending = [asyncio.ensure_future(label_stream.__anext__()), asyncio.ensure_future(box_stream.__anext__())]
            await asyncio.sleep(0.1)

            colors = {}
            steps = 0
            while not all(read.done() for read in pending):
                assert steps < 3, (
                    f"label and bounding box streams opened after {DEBOUNCE_FRAMES + 15} color-only captures "
                    f"had not both been served {steps} captures later"
                )
                color_read = asyncio.ensure_future(color_stream.__anext__())
                pending.append(color_read)
                await asyncio.sleep(0.05)
                await tc.step()
                color = await asyncio.wait_for(color_read, timeout=60.0)
                pending.remove(color_read)
                colors[color.header.sequence_id] = color
                steps += 1
                await asyncio.sleep(0.2)
            return before, pending[0].result(), pending[1].result(), colors
        finally:
            await _close(color_stream, *pending)
            for stream in (label_stream, box_stream):
                if stream is not None:
                    await _close(stream)

    before, label, boxes, colors = asyncio.run(run())

    _assert_no_capture_dropped("color before the label stream", [c.header.sequence_id for c in before], skip=0, at_least=5)
    assert label.header.sequence_id == boxes.header.sequence_id, (
        f"the first label image and the first bounding boxes are of different captures: "
        f"{label.header.sequence_id} and {boxes.header.sequence_id}"
    )
    assert label.header.sequence_id in colors, (
        f"the first label image is of capture {label.header.sequence_id}, not one taken since the "
        f"stream opened ({sorted(colors)})"
    )
    color = colors[label.header.sequence_id]
    assert label.header.capture_time_s == color.header.capture_time_s
    assert (label.width_px, label.height_px) == (color.width_px, color.height_px)

    expected = _boxes_from_label_image(label)
    if not labeled_scene:
        pytest.skip("The level has no static meshes to label, so there was no label to look for.")
    assert expected, (
        "the first label image after opening the stream late has no labeled pixel, in a scene whose "
        "meshes are all labeled: it was answered from a capture that didn't render labels"
    )
    actual = {b.instance_id: [b.min_x_px, b.min_y_px, b.max_x_px, b.max_y_px] for b in boxes.bounding_boxes}
    assert actual == expected, f"bounding boxes {actual} disagree with the label image's {expected}"


def _byte_difference(a, b, stride=31):
    """(mean absolute difference, fraction differing by more than 16 levels) over every `stride`th
    byte of two equally sized images. The stride is coprime with the three channels, so it samples
    all of them, and it keeps a full frame to milliseconds of Python."""
    assert len(a) == len(b)
    pairs = list(zip(a[::stride], b[::stride]))
    differences = [abs(x - y) for x, y in pairs]
    return sum(differences) / len(differences), sum(1 for d in differences if d > 16) / len(differences)


def test_color_is_the_same_with_and_without_a_label_stream(camera_rig, labeled_scene, fixed_step):
    """The color image of a static scene must not depend on whether anyone is streaming labels.

    Labels ride in the same render as color, and a camera may leave out the part of that render
    only they need. Whatever it leaves out, color has to come out the same. Frames of a static
    scene still differ a little from one to the next (temporal anti-aliasing, exposure), so the
    comparison is against how much the color-only frames differ among themselves."""
    owner, sensor = camera_rig
    count = 6

    async def run():
        color_stream = ts.stream_color_images(owner=owner, sensor=sensor).__aiter__()
        label_stream = None
        label_reader = None
        try:
            # Settle: temporal history, exposure, and the camera into rendering color alone.
            await _settle_reading(color_stream, DEBOUNCE_FRAMES + 10)
            alone = await _step_reading(color_stream, count)

            label_stream = ts.stream_label_images(owner=owner, sensor=sensor).__aiter__()

            async def read_labels():
                async for _ in label_stream:
                    pass

            label_reader = asyncio.ensure_future(read_labels())
            await asyncio.sleep(0.1)
            await _step_reading(color_stream, 4)
            with_labels = await _step_reading(color_stream, count)
            return alone, with_labels
        finally:
            await _close(color_stream)
            if label_stream is not None:
                await _close(label_stream, label_reader)

    alone, with_labels = asyncio.run(run())

    noise = [_byte_difference(a.data, b.data) for a, b in zip(alone, alone[1:])]
    noise_mean = max(mean for mean, _ in noise)
    noise_fraction = max(fraction for _, fraction in noise)
    across = [_byte_difference(a.data, b.data) for a in alone[-2:] for b in with_labels]
    across_mean = max(mean for mean, _ in across)
    across_fraction = max(fraction for _, fraction in across)

    assert across_mean <= 3.0 * noise_mean + 1.0 and across_fraction <= 3.0 * noise_fraction + 0.005, (
        f"color changed when a label stream was opened: frames differ by {across_mean:.2f} levels on "
        f"average with {across_fraction:.2%} of bytes off by more than 16, against {noise_mean:.2f} and "
        f"{noise_fraction:.2%} between color-only frames of the same static scene"
    )


def test_streams_cancelled_and_reopened_while_stepping_leave_delivery_intact(camera_rig, lidar_rig, fixed_step):
    """Streams cancelled with a response on its way, again and again while the client steps, must
    leave the sim serving: the next stream gets every capture.

    A cancelled stream's request is still held by the sensor, and its response may be mid-decode on
    a worker thread, or queued behind a write, when the server learns the client has gone."""
    owner, sensor = camera_rig
    lidar_owner, lidar_sensor = lidar_rig

    async def churn(rounds):
        for index in range(rounds):
            streams = [
                ts.stream_color_images(owner=owner, sensor=sensor).__aiter__(),
                ts.stream_depth_images(owner=owner, sensor=sensor).__aiter__(),
                ts.stream_label_images(owner=owner, sensor=sensor).__aiter__(),
                ts.stream_bounding_boxes(owner=owner, sensor=sensor).__aiter__(),
                ts.stream_lidar_scans(owner=lidar_owner, sensor=lidar_sensor).__aiter__(),
            ]
            reads = [asyncio.ensure_future(stream.__anext__()) for stream in streams]
            await asyncio.sleep(0.05)
            await tc.step()
            # Three ways to leave, in turn: at once, with the responses probably mid-decode; a
            # moment later, with them probably being written; and a capture later, with the first
            # response read and the stream's next request registered. Never a step with a response
            # left unread: the sim finishes its writes before a frame's send, and a write the
            # client isn't reading doesn't finish.
            if index % 3 == 1:
                await asyncio.sleep(0.03)
            elif index % 3 == 2:
                await asyncio.wait_for(asyncio.gather(*reads), timeout=60.0)
                reads = [asyncio.ensure_future(stream.__anext__()) for stream in streams]
                await asyncio.sleep(0.05)
                await tc.step()
            for stream, read in zip(streams, reads):
                await _close(stream, read)

    async def run():
        await churn(18)
        return await _headers_while_stepping(
            {"depth": ts.stream_depth_images(owner=owner, sensor=sensor).__aiter__()}, 20)

    _pipelined(True)
    try:
        with _Rates([camera_rig, lidar_rig], fixed_step):
            received = asyncio.run(run())
    finally:
        _pipelined(False)

    headers = [message["header"] for message in received["depth"]]
    _assert_no_capture_dropped("depth after the churn", [h.sequence_id for h in headers])
    _assert_transmitted_after_capture("depth after the churn", headers)


# Keep this the last test in the file: it swaps the world out from under everything the fixtures
# above set up.
def test_reloading_the_level_with_streams_open_leaves_the_sim_serving(readback_level):
    """Loading a level while sensor streams are open and frames are flowing must not crash or hang
    the sim, and the new world's sensors must stream.

    The load destroys every sensor and takes the sensor service down and up again. Decodes may be
    in flight on worker threads, and readbacks on the GPU, for sensors that no longer exist."""
    cameras = _sensors_measuring(SensorsPb.MT_COLOR_IMAGE)
    if not cameras:
        pytest.skip(f"No camera found on {LEVEL}.")
    lidars = _sensors_measuring(SensorsPb.MT_LIDAR_SCAN)
    if not lidars:
        # Not removed afterwards: it goes with the level.
        added = _add_lidar()
        lidars = [added] if added else []

    async def stream_until_the_level_goes(seconds_before_load):
        streams = []
        for owner, sensor in cameras:
            streams.append(ts.stream_color_images(owner=owner, sensor=sensor).__aiter__())
            streams.append(ts.stream_depth_images(owner=owner, sensor=sensor).__aiter__())
        for owner, sensor in lidars:
            streams.append(ts.stream_lidar_scans(owner=owner, sensor=sensor).__aiter__())
        counts = [0] * len(streams)

        async def consume(index):
            async for _ in streams[index]:
                counts[index] += 1

        consumers = [asyncio.ensure_future(consume(index)) for index in range(len(streams))]
        try:
            await asyncio.sleep(seconds_before_load)
            try:
                await tc.load_level(level=LEVEL)
            except Exception:
                pass  # the transition can strand this call; see _load_level_and_wait
            # The old world's streams end, with an error or without, when their service goes down.
            await asyncio.wait(consumers, timeout=30.0)
        finally:
            for stream, consumer in zip(streams, consumers):
                await _close(stream, consumer)
        return counts

    async def one_frame(owner, sensor):
        stream = ts.stream_color_images(owner=owner, sensor=sensor).__aiter__()
        read = asyncio.ensure_future(stream.__anext__())
        try:
            return await asyncio.wait_for(read, timeout=60.0)
        finally:
            await _close(stream, read)

    tc.set_time_mode(time_mode=Time.TM_WALL_CLOCK)
    tc.play()
    _pipelined(True)
    try:
        counts = asyncio.run(stream_until_the_level_goes(2.0))
        assert any(counts), "no stream delivered anything before the level was reloaded, so nothing was in flight"

        # The same level again, so the name alone doesn't say the load has happened: wait for the
        # services to answer and for a camera of the new world to deliver a frame.
        deadline = time.monotonic() + LEVEL_LOAD_TIMEOUT_S
        image = None
        last = None
        while image is None and time.monotonic() < deadline:
            try:
                assert tc.get_current_level_name().level == LEVEL.rsplit("/", 1)[-1]
                new_cameras = _sensors_measuring(SensorsPb.MT_COLOR_IMAGE)
                if new_cameras:
                    image = asyncio.run(one_frame(*new_cameras[0]))
            except Exception as error:  # services are briefly down across the swap
                last = repr(error)
                time.sleep(0.5)
        assert image is not None, (
            f"no camera streamed within {LEVEL_LOAD_TIMEOUT_S}s of reloading {LEVEL} with streams open; "
            f"last error: {last}"
        )
        assert image.width_px > 0 and len(image.data) == image.width_px * image.height_px * 3
    finally:
        try:
            _pipelined(False)
        except Exception:
            pass
