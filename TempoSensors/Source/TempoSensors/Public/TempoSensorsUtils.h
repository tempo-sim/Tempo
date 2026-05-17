// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"

void TEMPOSENSORS_API OptimizeShowFlagsForNoColor(FEngineShowFlags& ShowFlags);

// Configure the given post-process settings, show flags, and ray-tracing toggle for a
// photorealistic-quality scene capture: Lumen GI + reflections, MegaLights, histogram auto exposure,
// the full set of color-relevant show flags (tonemapper, bloom, lens flares, vignette, DOF,
// ambient occlusion, volumetric fog, etc.), and ray tracing on. Applied to both UTempoCamera (always)
// and UTempoLidar (only when bColorEnabled is true).
void TEMPOSENSORS_API ApplyPhotorealisticRenderSettings(FPostProcessSettings& OutPostProcess,
	FEngineShowFlags& OutShowFlags, bool& OutUseRayTracingIfEnabled);
