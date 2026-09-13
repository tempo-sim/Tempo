// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "RHIDefinitions.h"
#include "SceneView.h"

// Rewarps the object-motion part of one view's motion vectors, in place, so that they span
// ExtrapolationFactor scene ticks instead of one. See TempoMotionVectorRewarp.usf for the math.
//
// VelocityTexture must be the scene velocity texture (rewritten in place through a UAV) and
// SceneDepthTexture the resolved scene depth; ViewRect is the view's rect within them. The pass
// is skipped when the factor is not greater than one or the velocity format is one it doesn't
// handle (only the engine's 2D and 3D 16-bit unorm encodings are supported).
TEMPOSENSORSSHADERS_API void AddTempoMotionVectorRewarpPass(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	FRDGTextureRef VelocityTexture,
	FRDGTextureRef SceneDepthTexture,
	const FIntRect& ViewRect,
	float ExtrapolationFactor);
