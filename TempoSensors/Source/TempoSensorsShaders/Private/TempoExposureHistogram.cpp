// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoExposureHistogram.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FTempoExposureHistogramCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTempoExposureHistogramCS);
	SHADER_USE_PARAMETER_STRUCT(FTempoExposureHistogramCS, FGlobalShader);

	static constexpr int32 ThreadGroupSize = 16;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputTexture)
		SHADER_PARAMETER(FUintVector2, InputSize)
		SHADER_PARAMETER(FVector3f, LuminanceWeights)
		SHADER_PARAMETER(float, LuminanceMin)
		SHADER_PARAMETER(float, HistogramScale)
		SHADER_PARAMETER(float, HistogramBias)
		SHADER_PARAMETER(float, BlackBucketInfluence)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, HistogramOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("HISTOGRAM_SIZE"), TempoExposureHistogramSize);
		OutEnvironment.SetDefine(TEXT("WEIGHT_SCALE"), TempoExposureHistogramWeightScale);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTempoExposureHistogramCS, "/Plugin/TempoSensors/Private/TempoExposureHistogram.usf", "MainCS", SF_Compute);

void AddTempoExposureHistogramPass(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	FRDGTextureRef Input,
	const FTempoExposureHistogramParameters& Parameters,
	FRDGBufferRef OutHistogram)
{
	check(Input && OutHistogram);

	FRDGBufferUAVRef HistogramUAV = GraphBuilder.CreateUAV(OutHistogram, PF_R32_UINT);
	AddClearUAVPass(GraphBuilder, HistogramUAV, 0u);

	// GetEyeAdaptationParameters: the histogram spans [LogMin, LogMax] (LogMin at least one stop below
	// LogMax), and luminance is floored at the bottom of that range in histogram mode.
	const float LogMax = Parameters.HistogramLogMax;
	const float LogMin = FMath::Min(Parameters.HistogramLogMin, LogMax - 1.0f);
	const float Scale = 1.0f / (LogMax - LogMin);

	const FIntPoint Size = Input->Desc.Extent;

	FTempoExposureHistogramCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTempoExposureHistogramCS::FParameters>();
	PassParameters->InputTexture = Input;
	PassParameters->InputSize = FUintVector2(Size.X, Size.Y);
	PassParameters->LuminanceWeights = Parameters.LuminanceWeights;
	PassParameters->LuminanceMin = FMath::Exp2(LogMin);
	PassParameters->HistogramScale = Scale;
	PassParameters->HistogramBias = -LogMin * Scale;
	PassParameters->BlackBucketInfluence = Parameters.BlackBucketInfluence;
	PassParameters->HistogramOutput = HistogramUAV;

	const TShaderMapRef<FTempoExposureHistogramCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("TempoExposureHistogram %dx%d", Size.X, Size.Y),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(Size, FTempoExposureHistogramCS::ThreadGroupSize));
}
