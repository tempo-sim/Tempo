# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Dependency-free colormaps for lidar / scalar visualization.

We avoid matplotlib so the client stays light. `scalar_to_rgb` uses a compact
viridis approximation via per-channel linear interpolation; `LABEL_LUT` is the
same golden-ratio instance palette the PyVista/Qt utils use, so colors are
consistent across Tempo's example clients.
"""

import colorsys

import numpy as np

# 11-stop viridis approximation (RGB in [0, 1]).
_VIRIDIS = np.array(
    [
        [0.267, 0.005, 0.329],
        [0.283, 0.141, 0.458],
        [0.254, 0.265, 0.530],
        [0.207, 0.372, 0.553],
        [0.164, 0.471, 0.558],
        [0.128, 0.567, 0.551],
        [0.135, 0.659, 0.518],
        [0.267, 0.749, 0.441],
        [0.478, 0.821, 0.318],
        [0.741, 0.873, 0.150],
        [0.993, 0.906, 0.144],
    ],
    dtype=np.float32,
)
_VIRIDIS_X = np.linspace(0.0, 1.0, len(_VIRIDIS), dtype=np.float32)


def scalar_to_rgb(values, vmin=None, vmax=None) -> np.ndarray:
    """Map a 1-D array of scalars to (N, 3) uint8 RGB using viridis.

    Normalizes over the data range unless explicit vmin/vmax are given.
    """
    v = np.asarray(values, dtype=np.float32).ravel()
    if v.size == 0:
        return np.zeros((0, 3), dtype=np.uint8)
    lo = float(np.min(v)) if vmin is None else vmin
    hi = float(np.max(v)) if vmax is None else vmax
    norm = np.clip((v - lo) / (hi - lo + 1e-6), 0.0, 1.0)
    rgb = np.empty((v.size, 3), dtype=np.float32)
    for c in range(3):
        rgb[:, c] = np.interp(norm, _VIRIDIS_X, _VIRIDIS[:, c])
    return (rgb * 255.0).astype(np.uint8)


def _index_to_rgb(index: int):
    """Golden-ratio instance/label color (matches TempoImageUtils.index_to_rgb)."""
    if index == 0:
        return 0, 0, 0
    phi = 0.618033988749895
    psi = 0.7548776662466927
    h = (index * phi) % 1.0
    s = 0.6 + 0.4 * ((index * psi) % 1.0)
    v = 0.7 + 0.3 * ((index * psi * 1.3) % 1.0)
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return int(r * 255), int(g * 255), int(b * 255)


# (256, 3) lookup table indexed by label / instance id.
LABEL_LUT = np.array([_index_to_rgb(i) for i in range(256)], dtype=np.uint8)


def label_color(index: int):
    return tuple(int(c) for c in LABEL_LUT[index % 256])
