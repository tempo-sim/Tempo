// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "RHIDefinitions.h"

// The engine's exposure histogram (HISTOGRAM_SIZE in PostProcessHistogramCommon.ush).
constexpr int32 TempoExposureHistogramSize = 64;

// Fixed-point weight of one texel in the histogram. A uint bucket holds 2^32 / 256 = 16.7M texels,
// more than a 4K image.
constexpr float TempoExposureHistogramWeightScale = 256.0f;

// The engine's histogram parameters (GetEyeAdaptationParameters), as resolved for the view.
struct FTempoExposureHistogramParameters
{
	FVector3f LuminanceWeights = FVector3f(1.0f / 3.0f);
	float HistogramLogMin = -8.0f;
	float HistogramLogMax = 4.0f;
	float BlackBucketInfluence = 0.0f;
};

// Bins Input's luminance into OutHistogram (TempoExposureHistogramSize uints, cleared here) the way
// the engine's exposure histogram does. Each texel adds TempoExposureHistogramWeightScale in total.
TEMPOSENSORSSHADERS_API void AddTempoExposureHistogramPass(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	FRDGTextureRef Input,
	const FTempoExposureHistogramParameters& Parameters,
	FRDGBufferRef OutHistogram);
