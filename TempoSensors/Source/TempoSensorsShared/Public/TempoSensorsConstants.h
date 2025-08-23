// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

// This is the largest float less than the largest uint32 (2^32 - 1).
// We use it to discretize the depth buffer into a uint32 pixel.
constexpr float GTempo_Max_Discrete_Depth = 4294967040.0;
