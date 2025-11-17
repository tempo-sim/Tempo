// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"

/**
 * Compute shader for extracting 2D bounding boxes from instance segmentation label images.
 *
 * This shader implements a two-pass algorithm:
 * 1. Parallel scan of all pixels with groupshared memory reduction
 * 2. Atomic min/max updates to global bounding box buffers
 *
 * Performance: ~100-1000x fewer global atomic operations compared to naive implementation.
 */
class FBoundingBoxExtractionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBoundingBoxExtractionCS)
	SHADER_USE_PARAMETER_STRUCT(FBoundingBoxExtractionCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LabelTexture)  // Untyped for UNORM8 texture
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, BBoxMinBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, BBoxMaxBuffer)
		SHADER_PARAMETER(uint32, ImageWidth)
		SHADER_PARAMETER(uint32, ImageHeight)
	END_SHADER_PARAMETER_STRUCT()

	/**
	 * Determines if this shader should be compiled for the given platform.
	 * Requires SM5 (Shader Model 5) minimum for compute shader support.
	 */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/**
	 * Modifies the shader compilation environment.
	 * Sets thread group size constant for the shader.
	 */
	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), 16);
	}
};
