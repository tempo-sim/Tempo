# Copyright Tempo Simulation, LLC. All Rights Reserved

"""The ego vehicle: a car CitySample's Mass traffic is already driving, adopted over the API.

Mass keeps doing the driving -- lane following, traffic lights, yielding to the other cars -- so the
ego behaves as part of the city rather than as an intruder in it, and no Tempo controller is
involved.

How it works, and why each step:

- **Candidates.** Near the viewer, Mass swaps its low-detail stand-ins for physics actors
  (``CitySampleVehicleBase``, the ``BP_*_Sandbox`` Blueprints). Those are the cars that drive and can
  carry sensors. Parked cars are physics actors too, but tagged ``ParkedVehicle`` and never driven,
  so they are skipped.
- **Keep-alive.** A physics actor exists only while it is near a Mass LOD viewer, and the viewer is
  the player's camera. Pointing the player controller's view at the ego (``SetViewTargetWithBlend``,
  through the reflected API) makes the camera ride with it, so it stays a physics actor, and World
  Partition streams the city in around it.
- **Loss.** Mass can still release the actor -- at the end of its route, say -- and a pooled actor
  can come back as a different car. ``EgoWatch`` notices the actor disappearing, becoming parked or
  jumping, and the client adopts another car and mounts the rig again.
"""

import asyncio
import math
import random
import time
from dataclasses import dataclass
from typing import List, Tuple

from tempo_sim import world

import city_sample.reflected  # noqa: F401
from city_sample.reflected.CitySample import CitySampleVehicleBase
from city_sample.reflected.Engine import PlayerController, PrimitiveComponent
from city_sample.reflected.TempoSensors import TempoSceneCaptureComponent2D
from city_sample.reflected.enums import EViewTargetBlendFunction

import tempo_sim.tempo_sensors as ts
import tempo_sim.tempo_world as tw

from .config import VehicleCatalog, VehicleEntry
from .labels import EGO_TAG

PARKED_TAG = "ParkedVehicle"
STOPPED_SPEED_MPS = 0.5


@dataclass
class Ego:
    vehicle: CitySampleVehicleBase
    entry: VehicleEntry

    @property
    def name(self) -> str:
        return self.vehicle.actor


async def _traffic_vehicles(catalog: VehicleCatalog, allowed: List[VehicleEntry], exclude):
    """(handle, entry) for every physics vehicle traffic is driving whose class is allowed."""
    allowed_paths = {e.class_path for e in allowed}
    # The descriptors carry each actor's real class; a handle typed CitySampleVehicleBase is built
    # from each match, carrying that class so is_a() still answers for the real one.
    descriptors = (await tw.get_actors_of_class(actor_type=CitySampleVehicleBase.__tempo_class__,
                                                include_subclasses=True)).actors
    matches: List[Tuple[CitySampleVehicleBase, VehicleEntry]] = []
    for d in descriptors:
        entry = catalog.by_class(d.class_path)
        if entry is not None and d.class_path in allowed_paths and d.name not in exclude:
            matches.append((CitySampleVehicleBase(d.name, class_path=d.class_path), entry))
    if not matches:
        return []
    # Every actor's tags in one round trip rather than one per car.
    async with world.batch() as batch:
        tags = [batch.get(lambda h=h: h.Tags) for h, _ in matches]
    return [(h, e) for (h, e), t in zip(matches, tags) if PARKED_TAG not in t.value]


async def acquire(catalog: VehicleCatalog, choice: str, rng: random.Random,
                  exclude=(), timeout_s: float = 120.0) -> Ego:
    """Adopt a traffic vehicle matching `choice` (id, category or 'random'). Waits for one to come
    near the viewer, since traffic takes a while to populate after the level loads."""
    allowed = catalog.select(choice)
    deadline = time.monotonic() + timeout_s
    announced = False
    while True:
        candidates = await _traffic_vehicles(catalog, allowed, exclude)
        if candidates:
            # Prefer cars that are moving: one waiting at a light can wait a minute or more.
            moving = []
            for handle, entry in candidates:
                try:
                    v = (await tw.get_current_actor_state(actor=handle.actor)).velocity.linear
                except Exception:
                    continue
                if math.hypot(v.x, v.y) > 2.0:
                    moving.append((handle, entry))
            candidates = moving or candidates
            # A weighted pick among the vehicle types present, then any car of that type.
            present = {e.id: e for _, e in candidates}
            order = VehicleCatalog.weighted_order(list(present.values()), rng)
            chosen_entry = order[0]
            handle = rng.choice([h for h, e in candidates if e.id == chosen_entry.id])
            return Ego(handle, chosen_entry)
        if time.monotonic() > deadline:
            raise RuntimeError(
                f"No traffic vehicle matching --vehicle {choice!r} came near the viewer in "
                f"{timeout_s:.0f}s. Try another vehicle or category, or --vehicle random.")
        if not announced:
            print(f"  Waiting for a traffic vehicle matching {choice!r} ...")
            announced = True
        await asyncio.sleep(2.0)


async def adopt(ego: Ego) -> None:
    """Make the ego the player's view target (so Mass keeps it a physics actor and the city streams
    around it) and tag it so the labeler can tell it from other cars."""
    controllers = await world.get_actors_async(PlayerController)
    if not controllers:
        raise RuntimeError("No player controller to point at the ego vehicle.")
    pc = controllers[0]
    # A call (unlike an assignment) returns a value, so in a coroutine it is awaited on its own
    # rather than written inside a batch.
    await world.call_async(lambda: pc.SetViewTargetWithBlend(
        ego.vehicle, 0.0, EViewTargetBlendFunction.VTBlend_Linear, 0.0, False))
    # Tag the actor and every component that draws. The labeler resolves a component before its
    # actor, so the body's mesh rule (a van is a van) would beat an actor tag alone; a component
    # tag beats the mesh rule. labels.label_ego() then re-labels the car with the tags in place.
    components = await world.get_components_async(ego.vehicle, PrimitiveComponent)
    async with world.batch() as batch:
        actor_tags = batch.get(lambda: ego.vehicle.Tags)
        component_tags = [batch.get(lambda c=c: c.ComponentTags) for c in components]
    async with world.batch():
        if EGO_TAG not in actor_tags.value:
            ego.vehicle.Tags = list(actor_tags.value) + [EGO_TAG]
        for component, tags in zip(components, component_tags):
            if EGO_TAG not in tags.value:
                component.ComponentTags = list(tags.value) + [EGO_TAG]


RELEASED_TAG = "TempoReleasedEgo"


async def release(ego: Ego, sensor_names) -> None:
    """Hand a car the client is leaving back to the city: remove the rig's sensors and the TempoEgo
    tags, and have the labeler label it as an ordinary car again.

    Removing a tag doesn't re-label anything, and SetActorTagSemanticId re-labels only the actors
    carrying the tag it names. So the car gets a throwaway tag, that tag's (non-existent) override
    is cleared, which re-labels the car by its class and meshes, and the throwaway tag goes again."""
    for component in await world.get_components_async(ego.vehicle, TempoSceneCaptureComponent2D):
        if component.component in sensor_names:
            await world.destroy_component_async(component)
    components = await world.get_components_async(ego.vehicle, PrimitiveComponent)
    async with world.batch() as batch:
        actor_tags = batch.get(lambda: ego.vehicle.Tags)
        component_tags = [batch.get(lambda c=c: c.ComponentTags) for c in components]
    async with world.batch():
        ego.vehicle.Tags = [t for t in actor_tags.value if t != EGO_TAG] + [RELEASED_TAG]
        for component, tags in zip(components, component_tags):
            if EGO_TAG in tags.value:
                component.ComponentTags = [t for t in tags.value if t != EGO_TAG]
    await ts.set_actor_tag_semantic_id(actor_tag=RELEASED_TAG, semantic_id=-1)
    async with world.batch():
        ego.vehicle.Tags = [t for t in actor_tags.value if t != EGO_TAG]


class EgoWatch:
    """Polls the ego's state; ``lost`` is set when it is destroyed, parked, jumps (a pooled actor
    reused for another car), or has stood still for ``max_stopped_s`` of sim time.

    The last is not a failure of Mass but of the data: CitySample's traffic routinely waits a minute
    or more at lights and in jams, in Wall Clock and Fixed Step alike, and a stationary ego records
    the same frame over and over."""

    def __init__(self, ego: Ego, period_s: float = 1.0, max_jump_m: float = 30.0,
                 max_stopped_s: float = 30.0):
        self.ego = ego
        self.period_s = period_s
        self.max_jump_m = max_jump_m
        self.max_stopped_s = max_stopped_s
        self.lost = asyncio.Event()
        self.reason = ""

    async def run(self) -> None:
        last = None
        moving_at = None   # sim time the ego was last seen moving
        while not self.lost.is_set():
            await asyncio.sleep(self.period_s)
            try:
                state = await tw.get_current_actor_state(actor=self.ego.name)
                tags = await world.get_async(lambda: self.ego.vehicle.Tags)
            except Exception as exc:
                self._lose(f"actor gone ({exc.__class__.__name__})")
                return
            if PARKED_TAG in tags:
                self._lose("Mass parked it")
                return
            p = state.transform.location
            if last is not None:
                dt = max(state.timestamp_s - last[3], 1e-3)
                jump = math.dist((p.x, p.y, p.z), last[:3])
                if jump > self.max_jump_m and jump / dt > 60.0:
                    self._lose(f"jumped {jump:.0f} m (actor reused for another car)")
                    return
            last = (p.x, p.y, p.z, state.timestamp_s)
            v = state.velocity.linear
            if moving_at is None or math.hypot(v.x, v.y) > STOPPED_SPEED_MPS:
                moving_at = state.timestamp_s
            elif self.max_stopped_s > 0 and state.timestamp_s - moving_at > self.max_stopped_s:
                self._lose(f"stopped for {state.timestamp_s - moving_at:.0f} s of sim time")
                return

    def _lose(self, reason: str) -> None:
        self.reason = reason
        self.lost.set()
