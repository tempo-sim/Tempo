// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

// TempoCamera packs (label:8 | depth:24) into the 32 bits of a tile render target's fp32 alpha
// channel, so its discretized depth is limited to 24 bits (2^24 = 16777216). Used as the
// MaxDiscreteDepth parameter of the camera's distortion/label post-process material and as the
// divisor when decoding depth on CPU. The lidar reuses this 24-bit limit for both pixel formats:
// the WithColor format reserves the top 8 bits of each fp32 lane as a NaN/denormal-safety prefix,
// and the no-color format gives up its top depth byte to carry a reflectivity estimate.
constexpr float GTempoCamera_Max_Discrete_Depth = 16777216.0;
