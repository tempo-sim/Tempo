# TempoGeographic

TempoGeographic adds what a simulation needs in order to happen at a specific *place* and *time*
on Earth: a geographic anchor, a simulation clock mapped onto a real date and time, and a sun and
sky driven by both.

It is a small plugin today and will grow.

## Geographic reference

To anchor a simulation at a place on Earth you specify its **geographic reference** — the
geographic coordinate that the Cartesian origin corresponds to. Add an
`ATempoGeoReferencingSystem` to your level.

This class derives from Epic's GeoReferencing plugin's `AGeoReferencingSystem` and adds an event
notifying listeners that the geographic reference has changed.

!!! note "North-West-Up, not East-South-Up"

    Epic's GeoReferencing plugin is East-South-Up internally. Tempo composes a fixed −90° yaw
    correction so that `OriginRotation` presents **North-West-Up**, as documented — which is what
    the API and the ROS bridge both expect.

## Time of day

To convert simulation time into a date and time, specify the date and time that corresponds to
zero simulation time. Add an `ATempoDateTimeSystem` to your level; as simulation time progresses,
this actor tracks the corresponding date and time.

## Sun and sky

Once you know how to convert Unreal coordinates to geographic coordinates (via
`ATempoGeoReferencingSystem`) and simulation time to a date and time (via `ATempoDateTimeSystem`),
you can simulate the angle of the sun.

Add a **`TempoSunSky`** actor — a Blueprint class in TempoGeographic's Content folder — to your
level.

## Over the API

The geographic service lets a client set the geographic reference, set the date and time of day,
control how fast the day cycle runs relative to simulation time, and read the current date and
time back.

[:octicons-arrow-right-24: `TempoGeographic` RPCs](../reference/api/tempo-geographic.md)

The same operations are available as ROS services — see
[TempoROSBridge](tempo-ros-bridge.md#tempogeographic).
