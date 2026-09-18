// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidarParticipatingMedia.h"

#include "TempoSensorsShaders.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

// Renderer-private, reachable via the PrivateIncludePaths in TempoSensorsShaders.Build.cs. Only the
// shader parameter struct is used, which is header-only.
#include "LocalFogVolumeRendering.h"

namespace
{
	constexpr int32 ThreadGroupSize = 8;

	bool ShouldCompileMediaShader(const FGlobalShaderPermutationParameters& Parameters)
	{
		// Deferred renderer only: the volumetric fog grid and the fog constants mirrored here are
		// the deferred path's.
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsMobilePlatform(Parameters.Platform);
	}

	void SetCommonDefines(FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("OPTICAL_DEPTH_SCALE"), GTempoLidarMediaOpticalDepthScale);
	}

	// Bin geometry shared by both passes.
	struct FBinInputs
	{
		int32 NumBins = 0;
		float FirstBinEdge = 0.0f;
		float MaxRange = 0.0f;
		bool bValid = false;
	};

	FBinInputs MakeBinInputs(const FTempoLidarMediaSensorInputs& Sensor)
	{
		FBinInputs Bins;
		Bins.NumBins = FMath::Clamp(Sensor.NumBins, 2, 1024);
		Bins.MaxRange = Sensor.MaxRange;
		// The first edge has to sit strictly inside (0, MaxRange) for the log spacing to be defined.
		Bins.FirstBinEdge = FMath::Clamp(Sensor.FirstBinEdge, 1.0f, Sensor.MaxRange * 0.5f);
		Bins.bValid = Sensor.MaxRange > 2.0f;
		return Bins;
	}
}

class FTempoLidarMediaProfileCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTempoLidarMediaProfileCS);
	SHADER_USE_PARAMETER_STRUCT(FTempoLidarMediaProfileCS, FGlobalShader);

	class FUseVolumetricFog : SHADER_PERMUTATION_BOOL("USE_VOLUMETRIC_FOG");
	class FUseLocalFogVolumes : SHADER_PERMUTATION_BOOL("USE_LOCAL_FOG_VOLUMES");
	using FPermutationDomain = TShaderPermutationDomain<FUseVolumetricFog, FUseLocalFogVolumes>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, IntegratedLightScattering)
		SHADER_PARAMETER_SAMPLER(SamplerState, IntegratedLightScatteringSampler)
		SHADER_PARAMETER_STRUCT(FLocalFogVolumeUniformParameters, LFV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OpticalDepthOutput)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(FIntPoint, ViewRectSize)
		SHADER_PARAMETER(FVector4f, ExponentialFogParameters)
		SHADER_PARAMETER(FVector4f, ExponentialFogParameters2)
		SHADER_PARAMETER(FVector4f, ExponentialFogParameters3)
		SHADER_PARAMETER(float, MinFogTransmittance)
		SHADER_PARAMETER(float, FogEndDistance)
		SHADER_PARAMETER(float, VolumetricFogStartDistance)
		SHADER_PARAMETER(int32, NumBins)
		SHADER_PARAMETER(float, FirstBinEdge)
		SHADER_PARAMETER(float, MaxRange)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileMediaShader(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		SetCommonDefines(OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PROFILE_PASS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTempoLidarMediaProfileCS, "/Plugin/TempoSensors/Private/TempoLidarParticipatingMedia.usf", "ProfileCS", SF_Compute);

class FTempoLidarMediaResolveCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTempoLidarMediaResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FTempoLidarMediaResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, OpticalDepthProfile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint4>, Output)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(FIntPoint, ViewRectSize)
		SHADER_PARAMETER(int32, NumBins)
		SHADER_PARAMETER(float, FirstBinEdge)
		SHADER_PARAMETER(float, MaxRange)
		SHADER_PARAMETER(float, ExtinctionScale)
		SHADER_PARAMETER(float, Backscatter)
		SHADER_PARAMETER(float, IntensitySaturationDistance)
		SHADER_PARAMETER(uint32, bStochastic)
		SHADER_PARAMETER(uint32, Seed)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileMediaShader(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		SetCommonDefines(OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RESOLVE_PASS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTempoLidarMediaResolveCS, "/Plugin/TempoSensors/Private/TempoLidarParticipatingMedia.usf", "ResolveCS", SF_Compute);

FRDGTextureRef AddTempoLidarMediaProfilePass(FRDGBuilder& GraphBuilder, const FTempoLidarMediaPassInputs& Inputs)
{
	const FBinInputs Bins = MakeBinInputs(Inputs.Sensor);
	if (!Bins.bValid || Inputs.ViewRect.IsEmpty() || !Inputs.SceneDepth || !Inputs.ViewUniformBuffer.IsValid())
	{
		return nullptr;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TempoLidarMediaProfile");

	const FIntPoint RectSize = Inputs.ViewRect.Size();
	const FRDGTextureDesc ProfileDesc = FRDGTextureDesc::Create3D(
		FIntVector(RectSize.X, RectSize.Y, Bins.NumBins), PF_R32_UINT, FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef Profile = GraphBuilder.CreateTexture(ProfileDesc, TEXT("TempoLidarMedia.OpticalDepthProfile"));

	const bool bVolumetricFog = Inputs.Fog.IntegratedLightScattering != nullptr;
	const bool bLocalFogVolumes = Inputs.Fog.LocalFogVolumes != nullptr;

	FTempoLidarMediaProfileCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTempoLidarMediaProfileCS::FParameters>();
	PassParameters->View = Inputs.ViewUniformBuffer;
	PassParameters->SceneDepthTexture = Inputs.SceneDepth;
	PassParameters->IntegratedLightScattering = Inputs.Fog.IntegratedLightScattering;
	PassParameters->IntegratedLightScatteringSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	if (bLocalFogVolumes)
	{
		PassParameters->LFV = *Inputs.Fog.LocalFogVolumes;
	}
	PassParameters->OpticalDepthOutput = GraphBuilder.CreateUAV(Profile);
	PassParameters->ViewRectMin = Inputs.ViewRect.Min;
	PassParameters->ViewRectSize = RectSize;
	PassParameters->ExponentialFogParameters = Inputs.Fog.ExponentialFogParameters;
	PassParameters->ExponentialFogParameters2 = Inputs.Fog.ExponentialFogParameters2;
	PassParameters->ExponentialFogParameters3 = Inputs.Fog.ExponentialFogParameters3;
	PassParameters->MinFogTransmittance = Inputs.Fog.MinFogTransmittance;
	PassParameters->FogEndDistance = Inputs.Fog.EndDistance;
	PassParameters->VolumetricFogStartDistance = Inputs.Fog.VolumetricFogStartDistance;
	PassParameters->NumBins = Bins.NumBins;
	PassParameters->FirstBinEdge = Bins.FirstBinEdge;
	PassParameters->MaxRange = Bins.MaxRange;

	FTempoLidarMediaProfileCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FTempoLidarMediaProfileCS::FUseVolumetricFog>(bVolumetricFog);
	PermutationVector.Set<FTempoLidarMediaProfileCS::FUseLocalFogVolumes>(bLocalFogVolumes);
	const TShaderMapRef<FTempoLidarMediaProfileCS> ComputeShader(GetGlobalShaderMap(Inputs.FeatureLevel), PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("TempoLidarMediaProfile %dx%d x%d bins%s%s", RectSize.X, RectSize.Y, Bins.NumBins,
			bVolumetricFog ? TEXT(" +VolumetricFog") : TEXT(""), bLocalFogVolumes ? TEXT(" +LocalFogVolumes") : TEXT("")),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(RectSize, ThreadGroupSize));

	return Profile;
}

void AddTempoLidarMediaResolvePass(FRDGBuilder& GraphBuilder, const FTempoLidarMediaPassInputs& Inputs, FRDGTextureRef OpticalDepthProfile, FRDGTextureRef Output)
{
	const FBinInputs Bins = MakeBinInputs(Inputs.Sensor);
	if (!Bins.bValid || Inputs.ViewRect.IsEmpty() || !Inputs.SceneDepth || !OpticalDepthProfile || !Output || !Inputs.ViewUniformBuffer.IsValid())
	{
		return;
	}
	if (!EnumHasAnyFlags(Output->Desc.Flags, TexCreate_UAV) || Output->Desc.Format != PF_R16G16B16A16_UINT)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogTempoSensorsShaders, Warning, TEXT("Lidar media resolve skipped: output texture must be PF_R16G16B16A16_UINT with a UAV (got %s)."), GPixelFormats[Output->Desc.Format].Name);
		}
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TempoLidarMediaResolve");

	const FIntPoint RectSize = Inputs.ViewRect.Size();

	FTempoLidarMediaResolveCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTempoLidarMediaResolveCS::FParameters>();
	PassParameters->View = Inputs.ViewUniformBuffer;
	PassParameters->SceneDepthTexture = Inputs.SceneDepth;
	PassParameters->OpticalDepthProfile = OpticalDepthProfile;
	PassParameters->Output = GraphBuilder.CreateUAV(Output);
	PassParameters->ViewRectMin = Inputs.ViewRect.Min;
	PassParameters->ViewRectSize = RectSize;
	PassParameters->NumBins = Bins.NumBins;
	PassParameters->FirstBinEdge = Bins.FirstBinEdge;
	PassParameters->MaxRange = Bins.MaxRange;
	PassParameters->ExtinctionScale = FMath::Max(Inputs.Sensor.ExtinctionScale, 0.0f);
	PassParameters->Backscatter = FMath::Max(Inputs.Sensor.Backscatter, 0.0f);
	PassParameters->IntensitySaturationDistance = FMath::Max(Inputs.Sensor.IntensitySaturationDistance, 1.0f);
	PassParameters->bStochastic = Inputs.Sensor.bStochastic ? 1u : 0u;
	PassParameters->Seed = Inputs.Sensor.Seed;

	const TShaderMapRef<FTempoLidarMediaResolveCS> ComputeShader(GetGlobalShaderMap(Inputs.FeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("TempoLidarMediaResolve %dx%d", RectSize.X, RectSize.Y),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(RectSize, ThreadGroupSize));
}
