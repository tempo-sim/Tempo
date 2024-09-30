# TempoGeographic
TempoGeographic includes features necessary for a simulation that involves a specific time and location on Earth.

Currently, it only includes a couple of preliminary features. It will grow over time.

## Geographic Reference
To anchor a simulation at a specific location on Earth, we must specify it's "Geographic Reference", the geographic coordinate that its cartesian origin corresponds to. To do so, we must add an `ATempoGeoReferencingSystem` to our level. This class derives from the `GeoReferencing` plugin's `AGeoReferencingSystem`, and only adds an event to notify listeners that the Geographic Reference has changed.

## Time of Day
To convert simulation time to a specific date and time, we must specify the date and time that corresponds to zero simulation time. To do so, we must add an `ATempoDateTimeSystem` to our level. As simulation time progresses, this actor will keep track of the corresponding date and time.

## Sun & Sky
Once we know how to convert from Unreal coordinates to geographic coordinates (with our `ATempoGeoReferencingSystem` Actor) and how to convert simulation time to date and time (with our `ATempoDateTimeSystem` Actor), we can simulate the angle of the sun by adding a `TempoSunSky` Actor (a Blueprint class in `TempoGeographic`'s Content folder) to our level.
