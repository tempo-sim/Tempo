// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "HAL/Platform.h"

// TempoCamera packs a label and a discretized depth into the 32 bits of a tile render target's
// fp32 alpha channel, then recovers them with asuint() in the stitch aux pass. The alpha is the
// post-process material's opacity output, which the engine saturate()s (GetMaterialOpacity), so
// the packed float must lie in [0, 1); and it must be a normal float, since GPUs flush subnormals
// to zero. The valid bit patterns are therefore 0x00800000..0x3F7FFFFF -- exactly 252 * 2^22 of
// them -- and the layout is
//
//   bits = 0x00800000 + (label << 22) + depth22      (label 0..251, depth22 0..2^22-1)
//
// The stitch pass widens depth22 to the 24-bit code (<< 2) the merge pass and the CPU decode use,
// so the camera's depth is quantized to 22 bits of inverse depth.
//
// Two earlier layouts each broke a range of labels. The label used to occupy the raw top byte,
// which left the exponent zero for labels 0 and 128 -- a subnormal, decoding to MaxDepth. Then it
// rode in the exponent field biased by +1, with depth in the sign bit and mantissa: every label
// from 126 up made the alpha >= 1, which the engine's saturate turned into exactly 1.0, so all of
// them read back as label 126 at MaxDepth (and the sign bit, depth's top bit, was clamped to 0).
// Semantic labels are small, so only instance labels showed it.
//
// Discretized depth is limited to 24 bits (2^24 = 16777216). Used as the divisor when decoding
// depth on CPU. The lidar reuses this 24-bit limit for both pixel formats: the WithColor
// format reserves the top 8 bits of each fp32 lane as a NaN/denormal-safety prefix, and the
// no-color format gives up its top depth byte to carry a reflectivity estimate.
constexpr float GTempoCamera_Max_Discrete_Depth = 16777216.0;

// Largest label ID the camera's alpha encoding can represent (see above): label IDs run 0..251.
// The material clamps a larger one to this, so its pixels take the wrong label.
constexpr int32 GTempoCamera_Max_Label = 251;
