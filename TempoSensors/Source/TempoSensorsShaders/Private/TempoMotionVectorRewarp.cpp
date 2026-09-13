// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMotionVectorRewarp.h"

#include "TempoSensorsShaders.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FTempoMotionVectorRewarpCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTempoMotionVectorRewarpCS);
	SHADER_USE_PARAMETER_STRUCT(FTempoMotionVectorRewarpCS, FGlobalShader);

	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VelocityInput)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, VelocityOutput)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(FIntPoint, ViewRectSize)
		SHADER_PARAMETER(float, ExtrapolationFactor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// Deferred renderer only: the pass hooks the deferred base pass, and the mobile velocity
		// encoding is different.
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTempoMotionVectorRewarpCS, "/Plugin/TempoSensors/Private/TempoMotionVectorRewarp.usf", "MainCS", SF_Compute);

void AddTempoMotionVectorRewarpPass(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	FRDGTextureRef VelocityTexture,
	FRDGTextureRef SceneDepthTexture,
	const FIntRect& ViewRect,
	float ExtrapolationFactor)
{
	check(VelocityTexture && SceneDepthTexture);

	if (ViewRect.IsEmpty() || ExtrapolationFactor <= 1.0f)
	{
		return;
	}

	// FVelocityRendering::GetFormat: G16R16 (2D) or A16B16G16R16 (3D, when Lumen or ray tracing
	// are supported). The GLES uint variants are not handled.
	const EPixelFormat Format = VelocityTexture->Desc.Format;
	if (Format != PF_A16B16G16R16 && Format != PF_G16R16)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogTempoSensorsShaders, Warning, TEXT("Motion vector rewarp skipped: unsupported velocity format %s."), GPixelFormats[Format].Name);
		}
		return;
	}

	if (!EnumHasAnyFlags(VelocityTexture->Desc.Flags, TexCreate_UAV))
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TempoMotionVectorRewarp");

	// A pass can't read and write the same texture, so snapshot this view's rect of it. Only the
	// rect is copied: with several tiles in one family each view rewarps its own region.
	const FIntPoint RectSize = ViewRect.Size();
	const FRDGTextureDesc CopyDesc = FRDGTextureDesc::Create2D(RectSize, Format, FClearValueBinding::None, TexCreate_ShaderResource);
	FRDGTextureRef VelocityCopy = GraphBuilder.CreateTexture(CopyDesc, TEXT("TempoMotionVectorRewarp.VelocityCopy"));

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.SourcePosition = FIntVector(ViewRect.Min.X, ViewRect.Min.Y, 0);
	CopyInfo.DestPosition = FIntVector::ZeroValue;
	CopyInfo.Size = FIntVector(RectSize.X, RectSize.Y, 1);
	AddCopyTexturePass(GraphBuilder, VelocityTexture, VelocityCopy, CopyInfo);

	FTempoMotionVectorRewarpCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTempoMotionVectorRewarpCS::FParameters>();
	PassParameters->View = ViewUniformBuffer;
	PassParameters->VelocityInput = VelocityCopy;
	PassParameters->SceneDepthTexture = SceneDepthTexture;
	PassParameters->VelocityOutput = GraphBuilder.CreateUAV(VelocityTexture);
	PassParameters->ViewRectMin = ViewRect.Min;
	PassParameters->ViewRectSize = RectSize;
	PassParameters->ExtrapolationFactor = ExtrapolationFactor;

	const TShaderMapRef<FTempoMotionVectorRewarpCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("TempoMotionVectorRewarp %dx%d x%.2f", RectSize.X, RectSize.Y, ExtrapolationFactor),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(RectSize, FTempoMotionVectorRewarpCS::ThreadGroupSize));
}
