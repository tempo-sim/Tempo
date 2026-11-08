# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Semantic labels for CitySample, from config/labels.json.

Tempo labels the world from a table of rows, each matching actors by class or tag and components by
mesh asset. No binary asset is involved: the table is JSON, sent with ``LoadLabelTable``. But a
table can't simply list every mesh in CitySample: validation loads each mesh it names, and the city
has thousands of them. So the work is split by when it can be known:

- **Classes and tags** go into the table up front. A handful of rows.
- **Meshes** are matched by the regexes in labels.json against what is *loaded*, as World Partition
  streams it in. The meshes loaded at startup go into the table too (loading them again is free);
  each later poll assigns newly streamed meshes with ``SetStaticMeshTypeSemanticId``, which the
  labeler applies to every component using that mesh, now and later.

The ego vehicle is told apart from other cars by a tag, ``TempoEgo``, which the client adds when it
adopts the car: to the actor, and to each of its components. The component tags are what count --
the labeler resolves a component before its actor, so the van body's mesh would otherwise label it a
van whatever the actor's tag said.

Label type: Semantic by default. Instance mode (``--label-type instance``) is what 2D bounding boxes
need, but its ids are 1..253 shared by every labeled thing in view -- and in a city every mesh-
labeled building part is a thing -- so ids are reused, and an id can stand for more than one class
at once. Use it for the boxes of dynamic objects, and treat its semantic recovery as approximate.
"""

import asyncio
import json
import re
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import tempo_sim.tempo_sensors as ts
import tempo_sim.TempoSensors.Labels_pb2 as Labels
from tempo_sim.unreal import registered_classes

from .config import LabelClass

EGO_TAG = "TempoEgo"


@dataclass
class _CompiledClass:
    entry: LabelClass
    static: List[re.Pattern]
    skeletal: List[re.Pattern]


class LabelManager:
    def __init__(self, classes: List[LabelClass], label_type: str = "semantic"):
        self.classes = classes
        self.label_type = label_type
        self._compiled = [_CompiledClass(c, [re.compile(p) for p in c.static_mesh_patterns],
                                         [re.compile(p) for p in c.skeletal_mesh_patterns])
                          for c in classes]
        self._assigned: Dict[str, int] = {}      # mesh path -> semantic id the client set
        self._seen: set = set()                  # every mesh path looked at, matched or not
        self.unmatched: Dict[str, int] = {}      # mesh path -> instance count, for coverage reports

    # -- classification -------------------------------------------------------------------------

    def classify(self, mesh_path: str, skeletal: bool) -> Optional[int]:
        """The semantic id the rules give a mesh, or None. First class in file order wins."""
        for c in self._compiled:
            for pattern in (c.skeletal if skeletal else c.static):
                if pattern.search(mesh_path):
                    return c.entry.id
        return None

    @property
    def ego_id(self) -> Optional[int]:
        for c in self.classes:
            if EGO_TAG in c.actor_tags:
                return c.id
        return None

    def annotation_infos(self) -> List[Tuple[int, str, Tuple[int, int, int]]]:
        """(id, name, color) per class, for rerun's AnnotationContext."""
        return [(0, "unlabeled", (0, 0, 0))] + [(c.id, c.name, c.color) for c in self.classes]

    # -- installing -----------------------------------------------------------------------------

    async def _loaded_meshes(self):
        static = (await ts.get_all_static_mesh_types()).mesh_types
        skeletal = (await ts.get_all_skeletal_mesh_types()).mesh_types
        return [(m.mesh_path, m.instance_count, False) for m in static] + \
               [(m.mesh_path, m.instance_count, True) for m in skeletal]

    def _actor_classes(self, entry: LabelClass) -> List[str]:
        """The class paths of a row that this build has. The table import rejects the *whole*
        table over one unresolvable path, so a class the packaged build lacks (a different
        CitySample version, say) is dropped here with a warning instead."""
        registry = registered_classes()
        kept = []
        for path in entry.actor_classes:
            try:
                known = registry.python_class(path) is not None
            except Exception:
                known = True   # no generated package to ask; let the server decide
            if known:
                kept.append(path)
            else:
                print(f"  [labels] '{entry.name}': this build has no class {path}; skipping it")
        return kept

    async def install(self) -> None:
        """Load the table (classes, tags, and the meshes loaded right now) and set the label type."""
        meshes = await self._loaded_meshes()
        static_rows: Dict[int, List[str]] = {}
        skeletal_rows: Dict[int, List[str]] = {}
        for path, count, skeletal in meshes:
            self._seen.add(path)
            sid = self.classify(path, skeletal)
            if sid is None:
                self.unmatched[path] = count
                continue
            (skeletal_rows if skeletal else static_rows).setdefault(sid, []).append(path)
            self._assigned[path] = sid

        rows = [{"Name": "NoLabel", "Label": 0}]
        for c in self.classes:
            row = {"Name": c.name, "Label": c.id}
            actor_classes = self._actor_classes(c)
            if actor_classes:
                row["ActorTypes"] = actor_classes
            if c.actor_tags:
                row["ActorTags"] = c.actor_tags
            if c.component_tags:
                row["ComponentTags"] = c.component_tags
            if c.id in static_rows:
                row["StaticMeshTypes"] = sorted(static_rows[c.id])
            if c.id in skeletal_rows:
                row["SkeletalMeshTypes"] = sorted(skeletal_rows[c.id])
            rows.append(row)

        await ts.load_label_table(json=json.dumps(rows))
        await ts.set_label_type(label_type=Labels.LT_INSTANCE if self.label_type == "instance"
                                else Labels.LT_SEMANTIC)
        print(f"  Label table: {len(self.classes)} classes, {len(self._assigned)} of "
              f"{len(meshes)} loaded meshes matched ({self.label_type} labels).")

    async def sync_meshes(self) -> int:
        """Assign classes to meshes streamed in since the last call. Returns how many."""
        assigned = 0
        for path, count, skeletal in await self._loaded_meshes():
            if path in self._seen:
                if path in self.unmatched:
                    self.unmatched[path] = count
                continue
            self._seen.add(path)
            sid = self.classify(path, skeletal)
            if sid is None:
                self.unmatched[path] = count
                continue
            if skeletal:
                await ts.set_skeletal_mesh_type_semantic_id(skeletal_mesh_path=path, semantic_id=sid)
            else:
                await ts.set_static_mesh_type_semantic_id(static_mesh_path=path, semantic_id=sid)
            self._assigned[path] = sid
            assigned += 1
        return assigned

    async def run_mesh_sync(self, period_s: float = 5.0) -> None:
        """Keep assigning streamed-in meshes, forever (run as a task)."""
        while True:
            await asyncio.sleep(period_s)
            try:
                n = await self.sync_meshes()
                if n:
                    print(f"  [labels] classified {n} newly streamed mesh(es)")
            except Exception as exc:
                print(f"  [labels] mesh sync failed: {exc}")

    async def label_ego(self) -> None:
        """Re-label actors tagged TempoEgo. Adding a tag doesn't re-label anything by itself; this
        RPC un-labels and re-labels every actor carrying it, components and all, which is when the
        components' own TempoEgo tags are seen."""
        if self.ego_id is not None:
            await ts.set_actor_tag_semantic_id(actor_tag=EGO_TAG, semantic_id=self.ego_id)

    def coverage_report(self, top: int = 40) -> str:
        """The unmatched loaded meshes, most-instanced first: where to add rules next."""
        lines = [f"{len(self._assigned)} meshes matched, {len(self.unmatched)} unmatched. "
                 f"Top unmatched by instance count:"]
        for path, count in sorted(self.unmatched.items(), key=lambda x: -x[1])[:top]:
            lines.append(f"  {count:6d}  {path}")
        return "\n".join(lines)
