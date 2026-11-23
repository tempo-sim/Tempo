# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Instance -> semantic label remapping.

Tempo's label type is global: in Semantic mode every camera label pixel and lidar return carries a
semantic class id; in Instance mode it carries an instance id (1..251, reused), and
``get_instance_to_semantic_id_map`` says which class each instance currently belongs to. Instance
mode is the richer one (2D bounding boxes need it, and semantic classes are recoverable from it),
so a client that wants both keeps one of these up to date and passes it to the loggers.

The map changes as actors spawn and are destroyed, so ``refresh`` should be called periodically.
Swapping the lookup table is a single reference assignment, so loggers on worker threads always
see either the old table or the new one, never a half-written one.
"""

import numpy as np

import tempo_sim.tempo_sensors as ts


class LabelRemap:
    def __init__(self, fixed_ids=()):
        """`fixed_ids`: ids that always map to themselves, whatever the labeler's map says. The
        labeler reserves every class's label as an instance id meaning the whole class, but its map
        lists only ids some labeled object holds. A stuff class with nothing loaded, or one reached
        only through a per-pixel label override, would otherwise read as unlabeled."""
        self._fixed = [i for i in fixed_ids if 0 < i < 256]
        # Identity until the first refresh: an unknown instance maps to itself, which in Semantic
        # mode is exactly right.
        self._lut = np.arange(256, dtype=np.uint8)

    @property
    def lut(self) -> np.ndarray:
        return self._lut

    def semantic(self, instance_ids: np.ndarray) -> np.ndarray:
        """Map an array of instance ids (any integer dtype) to semantic ids (uint8)."""
        return self._lut[np.asarray(instance_ids).astype(np.intp) & 0xFF]

    def set_mapping(self, pairs) -> None:
        """Replace the mapping from an iterable of (instance_id, semantic_id)."""
        lut = np.zeros(256, dtype=np.uint8)
        lut[self._fixed] = self._fixed
        for instance_id, semantic_id in pairs:
            if 0 <= instance_id < 256:
                lut[instance_id] = semantic_id
        self._lut = lut

    async def refresh(self) -> None:
        response = await ts.get_instance_to_semantic_id_map()
        self.set_mapping((m.instance_id, m.semantic_id) for m in response.instance_semantic_id_pairs)


class ActorLabels:
    """Actor name -> the semantic class the labeler gave the actor, from ``get_all_actor_labels``.

    The same names the TempoWorld streams report, so a ground-truth box can be classed without a
    lookup per actor. Like LabelRemap, refresh periodically; the dict is swapped whole.
    """

    def __init__(self):
        self._by_name: dict = {}

    def semantic(self, actor_name: str):
        """The actor's semantic id, or None if the labeler hasn't labeled it (yet)."""
        return self._by_name.get(actor_name)

    async def refresh(self) -> None:
        response = await ts.get_all_actor_labels()
        self._by_name = {a.actor_name: a.semantic_id for a in response.actors}
