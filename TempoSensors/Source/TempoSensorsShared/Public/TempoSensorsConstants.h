// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

// This is the largest float less than the largest uint32 (2^32 - 1).
// We use it to discretize the depth buffer into a uint32 pixel.
constexpr float GTempo_Max_Discrete_Depth = 4294967040.0;

// TempoCamera packs (label:8 | depth:24) into the 32 bits of a tile render target's fp32 alpha
// channel, so its discretized depth is limited to 24 bits (2^24 = 16777216). Used as the
// MaxDiscreteDepth parameter of the camera's distortion/label post-process material and as the
// divisor when decoding depth on CPU.
constexpr float GTempoCamera_Max_Discrete_Depth = 16777216.0;
