# Copyright Tempo Simulation, LLC. All Rights Reserved

"""CitySample's own lighting controls, the two the in-game HUD offers under World options.

CitySample does not use TempoGeographic: its sun is a directional light under a painted sky dome,
driven by two pieces of the project, both reached here through the reflected API.

- **Sun angle.** ``ACitySampleWorldInfo::SetSunlightAngle``, what the HUD's "Sun Rotation" slider
  calls. It sets the sun's *yaw* -- its compass direction -- and turns the sky dome with it. The
  sun's elevation is not adjustable: tilting the light alone would cast shadows the painted sky
  disagrees with.
- **Night mode.** ``BP_nightmode.Set_nightTime``, what the HUD's Night Mode toggle ends up running:
  the HUD fires the game state's ``OnTriggerNighttime`` event, and ``BP_nightmode`` is the one
  listener that changes what a sensor sees (sun, skylight, fog and sky). An event can't be fired
  over the API, so its handler is called directly.
"""

from typing import Optional

from tempo_sim import world

import city_sample.reflected  # noqa: F401
from city_sample.reflected.CitySample import CitySampleWorldInfo
from city_sample.reflected.Game.Gameplay.Framework import BP_nightmode_C


async def apply(sun_angle_deg: Optional[float] = None, night: bool = False) -> None:
    """Set the sun's compass direction (degrees, as the HUD slider) and/or switch to night. Nothing
    is changed for an argument left at its default."""
    if sun_angle_deg is not None:
        infos = await world.get_actors_async(CitySampleWorldInfo)
        if not infos:
            raise RuntimeError("--sun-angle: this level has no CitySampleWorldInfo actor to set it on.")
        info = infos[0]
        await world.call_async(lambda: info.SetSunlightAngle(sun_angle_deg))
        angle = await world.call_async(lambda: info.GetSunlightAngle())
        print(f"  Sun angle: {angle:.0f} deg.")
    if night:
        controllers = await world.get_actors_async(BP_nightmode_C)
        if not controllers:
            raise RuntimeError("--night: this level has no BP_nightmode actor (CitySample's night "
                               "mode lives in it).")
        nightmode = controllers[0]
        await world.call_async(lambda: nightmode.Set_nightTime())
        print("  Night mode on.")
