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

## Level requirements

The scene-independent checks (band seams, cross-type agreement, sequence continuity) run against
whatever level the packaged sim loads. The sharpest checks — frame identity and tearing — need
geometry in front of the camera, and the test says so explicitly rather than passing vacuously:

  * `test_frame_matches_its_header` needs anything solid in front of the camera. It skips, with
    instructions, if the camera sees only sky.
  * `test_no_tearing_within_a_frame` needs a flat surface perpendicular to the view axis filling
    the frame, so that every pixel has the same depth and a mix of two captures is unmissable. It
    falls back to a weaker check otherwise.

To get the strongest version, point TEMPO_TEST_RIG_ORIGIN at a spot in your level facing a large
flat wall — or build a level for it:

  1. New Basic level. Delete the floor if it is in shot.
  2. Add a Cube, scale it to (1, 40, 40), place it at Unreal location (2000, 0, 0). That is a wall
     20 m down +X from the origin, spanning far enough to fill the frame.
  3. Add two or three distinct static meshes between x = 500 and x = 1500, offset in Y and Z so
     they do not overlap in the image. These give the bounding box cross-check something to chew
     on. Make sure they are labeled (see the TempoSensors label table).
  4. No moving actors, no animated or flickering lights: the test relies on the scene being static
     so that only the camera's motion changes the image.
  5. Save as e.g. /Game/Maps/ReadbackTest, set it as GameDefaultMap, and repackage.

Then run with the defaults (rig origin 0,0,0 looking down +X).

## Environment

  TEMPO_TEST_RIG_ORIGIN   "x,y,z" in meters, Tempo right-handed frame. Default "0,0,1".
  TEMPO_TEST_RIG_TYPE     Actor type to spawn. Default "BP_SensorRig".
"""

import asyncio
import math
import os
import statistics
import struct

import pytest

import tempo_sim.tempo_core as tc
import tempo_sim.tempo_sensors as ts
import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry

pytestmark = pytest.mark.sensors

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


@pytest.fixture
def camera_rig(fixed_step):
    """Spawn the sensor rig at a known pose with identity rotation and yield (owner, sensor).

    Identity rotation keeps the camera's forward axis along +X, which is what lets the test move
    the camera along its own view axis without reasoning about rotation conventions.
    """
    spawned = tw.spawn_actor(
        actor_type=RIG_TYPE,
        transform=Geometry.Transform(
            location=_vec(*RIG_ORIGIN), rotation=Geometry.Rotation(r=0.0, p=0.0, y=0.0)
        ),
    )
    owner = spawned.name
    assert owner, f"could not spawn {RIG_TYPE}"

    try:
        sensors = ts.get_available_sensors().available_sensors
        cam = next((s for s in sensors if s.owner == owner), None)
        if cam is None:
            pytest.skip(f"No sensor reported on {owner}; expected a camera on {RIG_TYPE}.")
        yield cam.owner, cam.name
    finally:
        tw.destroy_actor(actor=owner)


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

    # Establish the oracle before trusting it. If the very first frame disagrees we cannot tell a
    # regression from a scene that does not suit the test, so say so instead of crying wolf.
    loc = first.capture_transform.location
    probe = tw.raycast(
        start=_vec(loc.x, loc.y, loc.z),
        end=_vec(loc.x + MAX_RAY_M, loc.y, loc.z),
        ignored_actors=[owner],
    )
    if not probe.hit:
        pytest.skip(
            "Nothing in front of the camera to range against — the raycast missed. Point "
            "TEMPO_TEST_RIG_ORIGIN at a spot facing a wall, or build the level described in this "
            "file's docstring."
        )
    if abs(_center_depth(frames[0]["depth"]) - probe.distance_m) > DEPTH_TOL_M:
        pytest.skip(
            f"Could not establish the depth oracle: first frame center depth "
            f"{_center_depth(frames[0]['depth']):.3f} m vs raycast {probe.distance_m:.3f} m. The "
            "camera is probably not looking along +X, or something non-static is in shot."
        )

    # The oracle holds. From here a mismatch is a real disagreement between pixels and header.
    observed = []
    for index, frame in enumerate(frames):
        header = frame["depth"].header
        loc = header.capture_transform.location
        fwd = _forward_from_rotation(header.capture_transform.rotation)
        truth = tw.raycast(
            start=_vec(loc.x, loc.y, loc.z),
            end=_vec(loc.x + fwd[0] * MAX_RAY_M, loc.y + fwd[1] * MAX_RAY_M, loc.z + fwd[2] * MAX_RAY_M),
            ignored_actors=[owner],
        )
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

    Depth is measured along the camera axis, so a flat surface perpendicular to that axis reads the
    same value at every pixel. Two distinct depth populations in one frame therefore means two
    captures got mixed. Where the scene does not offer such a surface, fall back to checking that
    the frame is not a blend of its neighbours.
    """
    owner, sensor = camera_rig
    frames = asyncio.run(_capture_frames(owner, sensor, NUM_FRAMES))

    depths = [_depths(frame["depth"]) for frame in frames]
    center = _center_depth(frames[0]["depth"])
    finite = [d for d in depths[0] if math.isfinite(d)]
    if not finite:
        pytest.skip("First frame had no finite depths; nothing to check.")

    uniform_fraction = sum(1 for d in depths[0] if abs(d - center) <= DEPTH_TOL_M) / len(depths[0])
    if uniform_fraction > 0.95:
        # Strong form: a flat wall fills the frame, so every frame must be single-valued.
        for index, frame_depths in enumerate(depths):
            frame_center = _center_depth(frames[index]["depth"])
            outliers = sum(1 for d in frame_depths if abs(d - frame_center) > DEPTH_TOL_M)
            assert outliers / len(frame_depths) <= 0.05, (
                f"frame {index}: {outliers} of {len(frame_depths)} pixels disagree with the center "
                f"depth {frame_center:.3f} m. A flat wall fills this frame, so more than one depth "
                "population means two captures were mixed into one image."
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
    at a band edge, which lands at multiples of Height/32 and Height/64 and nowhere else. Comparing
    row-to-row change at those rows against the rest of the image turns that into a signal that
    does not depend on scene content.
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

        # Mean absolute difference between each row and the one above it.
        row_deltas = []
        for y in range(1, h):
            a = data[(y - 1) * row_bytes: y * row_bytes]
            b = data[y * row_bytes: (y + 1) * row_bytes]
            row_deltas.append(sum(abs(p - q) for p, q in zip(a, b)) / row_bytes)

        seam_rows = set()
        for bands in (32, 64):
            for band in range(1, bands):
                row = (h * band) // bands
                if 1 <= row < h:
                    seam_rows.add(row - 1)  # row_deltas[i] compares rows i and i+1

        seam = [row_deltas[i] for i in sorted(seam_rows)]
        rest = [d for i, d in enumerate(row_deltas) if i not in seam_rows]
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


def test_measurement_types_agree(camera_rig, pipelining):
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

        # Recompute the boxes from the label image and require an exact match.
        w = label.width_px
        expected = {}
        for i, instance_id in enumerate(label.data):
            if instance_id == 0:
                continue
            x, y = i % w, i // w
            box = expected.get(instance_id)
            if box is None:
                expected[instance_id] = [x, y, x, y]
            else:
                box[0] = min(box[0], x)
                box[1] = min(box[1], y)
                box[2] = max(box[2], x)
                box[3] = max(box[3], y)

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
