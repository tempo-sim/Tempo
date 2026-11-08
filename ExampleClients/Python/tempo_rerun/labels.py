# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Instance -> semantic label remapping.

Tempo's label type is global: in Semantic mode every camera label pixel and lidar return carries a
semantic class id; in Instance mode it carries an instance id (1..253, reused), and
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
    def __init__(self):
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
        for instance_id, semantic_id in pairs:
            if 0 <= instance_id < 256:
                lut[instance_id] = semantic_id
        self._lut = lut

    async def refresh(self) -> None:
        response = await ts.get_instance_to_semantic_id_map()
        self.set_mapping((m.instance_id, m.semantic_id) for m in response.instance_semantic_id_pairs)
