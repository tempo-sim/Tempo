// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "BoundingBoxComputeShader.h"
#include "ShaderCompilerCore.h"

/**
 * Implements and registers the BoundingBoxExtractionCS global shader.
 *
 * This macro:
 * 1. Generates shader compilation code
 * 2. Registers the shader with Unreal's shader system
 * 3. Maps the C++ class to the .usf file and entry point
 *
 * Parameters:
 * - FBoundingBoxExtractionCS: The C++ shader class
 * - "/TempoSensors/Private/BoundingBoxExtraction.usf": Virtual path to shader file
 * - "BBoxExtractionCS": Entry point function name in the .usf file
 * - SF_Compute: Shader frequency (compute shader)
 */
IMPLEMENT_GLOBAL_SHADER(
	FBoundingBoxExtractionCS,
	"/TempoSensors/Private/BoundingBoxExtraction.usf",
	"BBoxExtractionCS",
	SF_Compute
);
