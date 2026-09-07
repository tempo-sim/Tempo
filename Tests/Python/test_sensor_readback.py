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

# Depth is discretized inverse depth over [MinDepth, MaxDepth], so absolute precision degrades with
# range. A whole-frame confusion moves depth by a STEP_DELTAS_M entry (>= 0.2 m), so this is far
# tighter than needed to catch one while tolerating quantization.
DEPTH_TOL_M = 0.05

# Ray length for the ground-truth raycast, in meters.
MAX_RAY_M = 500.0

# How long to wait for the test level to come up. A level swap tears the gRPC services down and
# back up, so the first calls after it can fail before they start answering.
LEVEL_LOAD_TIMEOUT_S = 120.0


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

    frames = []
    x = RIG_ORIGIN[0]
    for index in range(num_frames):
        if move_camera:
            x += STEP_DELTAS_M[index % len(STEP_DELTAS_M)]
            await tw.set_actor_location(actor=owner, location=_vec(x, RIG_ORIGIN[1], RIG_ORIGIN[2]))

        # Have the next receive outstanding before stepping: a capture only happens when a request
        # is already pending, so issuing them after the step would race the capture.
        pending = [
            asyncio.ensure_future(stream.__anext__())
            for stream in (color_stream, label_stream, depth_stream, box_stream)
        ]
        await asyncio.sleep(0.05)

        await tc.step()

        color, label, depth, boxes = await asyncio.wait_for(asyncio.gather(*pending), timeout=60.0)
        frames.append({"color": color, "label": label, "depth": depth, "boxes": boxes})

    return frames


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
    # (GTempoCamera_Max_Label = 253); 0 means "unlabeled", so start at 1.
    assigned = []
    for index, mesh in enumerate(meshes[:253]):
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

        # Recompute the boxes from the label image and require an exact match. Row by row, with
        # bytes.index/rindex finding the extents at C speed; a per-pixel Python loop over a full
        # frame takes seconds.
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
