// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "HAL/Platform.h"

// TempoCamera packs a label and a discretized depth into the 32 bits of a tile render target's
// fp32 alpha channel, then recovers them with asuint() in the stitch aux pass. The layout keeps
// the alpha a normal, finite float for every possible (label, depth) pair:
//
//   bit 31     = depth bit 23
//   bits 30:23 = label + 1      (exponent field, 1..254)
//   bits 22:0  = depth bits 22:0
//
// The +1 bias on the exponent field is load-bearing. The label used to occupy the raw top byte,
// which left the exponent zero whenever the label's low 7 bits were zero (labels 0 and 128) --
// a subnormal, which GPUs flush to zero, decoding to MaxDepth for every such pixel. Labels whose
// low 7 bits were all ones (127 and 255) could reach exponent 255 and produce Inf/NaN instead.
// Biasing the field into 1..254 makes both ends unreachable, at the cost of capping label IDs
// (see GTempoCamera_Max_Label).
//
// Discretized depth is limited to 24 bits (2^24 = 16777216). Used as the MaxDiscreteDepth
// parameter of the camera's distortion/label post-process material and as the divisor when
// decoding depth on CPU. The lidar reuses this 24-bit limit for both pixel formats: the WithColor
// format reserves the top 8 bits of each fp32 lane as a NaN/denormal-safety prefix, and the
// no-color format gives up its top depth byte to carry a reflectivity estimate.
constexpr float GTempoCamera_Max_Discrete_Depth = 16777216.0;

// Largest label ID the camera's alpha encoding can represent. The label is stored biased by +1 in
// the fp32 exponent field, so the usable field is 1..254 and label IDs run 0..253. A label outside
// this range corrupts both the label and the depth of every pixel it covers.
constexpr int32 GTempoCamera_Max_Label = 253;
