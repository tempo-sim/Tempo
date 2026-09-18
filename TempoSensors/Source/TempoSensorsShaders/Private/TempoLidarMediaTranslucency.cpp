// Copyright Tempo Simulation, LLC. All Rights Reserved

// The lidar's participating media translucency pass: the plugin's own material shader types,
// compiled for every translucent surface material, and the mesh pass that draws the view's
// translucent batches with them. See TempoLidarMediaTranslucency.usf.

#include "TempoLidarParticipatingMedia.h"

#include "TempoSensorsShaders.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "InstanceCulling/InstanceCullingContext.h"
#include "MaterialShared.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneUniformBuffer.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

// Bound per draw by the pass's shaders: the profile they add to and how ranges map to its bins.
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FTempoLidarMediaTranslucencyPassUniformParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OpticalDepthProfile)
	SHADER_PARAMETER(FIntPoint, ViewRectMin)
	SHADER_PARAMETER(int32, NumBins)
	SHADER_PARAMETER(float, FirstBinEdge)
	SHADER_PARAMETER(float, MaxRange)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FTempoLidarMediaTranslucencyPassUniformParameters, "TempoLidarMediaPass");

BEGIN_SHADER_PARAMETER_STRUCT(FTempoLidarMediaTranslucencyPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
	// Materials that fade against the depth buffer (soft particles) read it through this.
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FTempoLidarMediaTranslucencyPassUniformParameters, Pass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

namespace
{
	// Which materials get this pass's shaders: translucent surface materials that block light.
	// Additive ones add light and block none; alpha holdout is a compositing tool, not a medium.
	// Everything else, opaque and masked included, already writes scene depth.
	bool ShouldCompileTranslucencyShaders(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) || IsMobilePlatform(Parameters.Platform))
		{
			return false;
		}
		const FMaterialShaderParameters& Material = Parameters.MaterialParameters;
		if (Material.MaterialDomain != MD_Surface || !IsTranslucentBlendMode(Material))
		{
			return false;
		}
		return Material.BlendMode != BLEND_Additive && Material.BlendMode != BLEND_AlphaHoldout;
	}

	bool IsMaterialForTranslucencyPass(const FMaterial& Material)
	{
		return Material.GetMaterialDomain() == MD_Surface && IsTranslucentBlendMode(Material)
			&& Material.GetBlendMode() != BLEND_Additive && Material.GetBlendMode() != BLEND_AlphaHoldout;
	}

	class FTempoLidarMediaTranslucencyShaderElementData : public FMeshMaterialShaderElementData
	{
	public:
		FRHIUniformBuffer* PassUniformBuffer = nullptr;
	};

	class FTempoLidarMediaTranslucencyShaderBase : public FMeshMaterialShader
	{
	public:
		FTempoLidarMediaTranslucencyShaderBase() = default;
		FTempoLidarMediaTranslucencyShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FMeshMaterialShader(Initializer)
		{
		}

		static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
		{
			return ShouldCompileTranslucencyShaders(Parameters);
		}

		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("OPTICAL_DEPTH_SCALE"), GTempoLidarMediaOpticalDepthScale);
		}

		void GetShaderBindings(
			const FScene* Scene,
			ERHIFeatureLevel::Type FeatureLevel,
			const FPrimitiveSceneProxy* PrimitiveSceneProxy,
			const FMaterialRenderProxy& MaterialRenderProxy,
			const FMaterial& Material,
			const FTempoLidarMediaTranslucencyShaderElementData& ShaderElementData,
			FMeshDrawSingleShaderBindings& ShaderBindings) const
		{
			FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, ShaderElementData, ShaderBindings);
			ShaderBindings.Add(GetUniformBufferParameter<FTempoLidarMediaTranslucencyPassUniformParameters>(), ShaderElementData.PassUniformBuffer);
		}
	};
}

class FTempoLidarMediaTranslucencyVS : public FTempoLidarMediaTranslucencyShaderBase
{
public:
	DECLARE_SHADER_TYPE(FTempoLidarMediaTranslucencyVS, MeshMaterial);

	FTempoLidarMediaTranslucencyVS() = default;
	FTempoLidarMediaTranslucencyVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FTempoLidarMediaTranslucencyShaderBase(Initializer)
	{
	}
};

class FTempoLidarMediaTranslucencyPS : public FTempoLidarMediaTranslucencyShaderBase
{
public:
	DECLARE_SHADER_TYPE(FTempoLidarMediaTranslucencyPS, MeshMaterial);

	FTempoLidarMediaTranslucencyPS() = default;
	FTempoLidarMediaTranslucencyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FTempoLidarMediaTranslucencyShaderBase(Initializer)
	{
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FTempoLidarMediaTranslucencyVS, TEXT("/Plugin/TempoSensors/Private/TempoLidarMediaTranslucency.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FTempoLidarMediaTranslucencyPS, TEXT("/Plugin/TempoSensors/Private/TempoLidarMediaTranslucency.usf"), TEXT("MainPS"), SF_Pixel);

namespace
{
	// Builds one draw command per translucent batch: depth-tested against the opaque scene, no
	// depth write, no color target. The pixel shader's only output is the profile.
	class FTempoLidarMediaTranslucencyMeshProcessor : public FMeshPassProcessor
	{
	public:
		FTempoLidarMediaTranslucencyMeshProcessor(const FScene* InScene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InView,
			FMeshPassDrawListContext* InDrawListContext, FRHIUniformBuffer* InPassUniformBuffer)
			: FMeshPassProcessor(EMeshPass::Num, InScene, InFeatureLevel, InView, InDrawListContext)
			, PassUniformBuffer(InPassUniformBuffer)
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			DrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilNop);
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_NONE>::GetRHI());
		}

		virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
		{
			if (!MeshBatch.bUseForMaterial)
			{
				return;
			}
			// Walk the material's fallback chain, as the engine's passes do, so a material whose
			// shaders are still compiling draws with its fallback rather than not at all.
			const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
			while (MaterialRenderProxy)
			{
				const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
				if (Material && Material->GetRenderingThreadShaderMap())
				{
					if (!IsMaterialForTranslucencyPass(*Material))
					{
						// A fallback is opaque; nothing further down the chain is for this pass.
						return;
					}
					if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, *MaterialRenderProxy, *Material))
					{
						return;
					}
				}
				MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
			}
		}

	private:
		bool TryAddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId,
			const FMaterialRenderProxy& MaterialRenderProxy, const FMaterial& Material)
		{
			TMeshProcessorShaders<FTempoLidarMediaTranslucencyVS, FTempoLidarMediaTranslucencyPS> PassShaders;

			FMaterialShaderTypes ShaderTypes;
			ShaderTypes.AddShaderType<FTempoLidarMediaTranslucencyVS>();
			ShaderTypes.AddShaderType<FTempoLidarMediaTranslucencyPS>();

			FMaterialShaders Shaders;
			if (!Material.TryGetShaders(ShaderTypes, MeshBatch.VertexFactory->GetType(), Shaders))
			{
				return false;
			}
			if (!Shaders.TryGetVertexShader(PassShaders.VertexShader) || !Shaders.TryGetPixelShader(PassShaders.PixelShader))
			{
				return false;
			}

			const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
			const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(Material, OverrideSettings);
			const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(Material, OverrideSettings);

			FTempoLidarMediaTranslucencyShaderElementData ShaderElementData;
			ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);
			ShaderElementData.PassUniformBuffer = PassUniformBuffer;

			const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

			BuildMeshDrawCommands(
				MeshBatch,
				BatchElementMask,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				Material,
				DrawRenderState,
				PassShaders,
				MeshFillMode,
				MeshCullMode,
				SortKey,
				EMeshPassFeatures::Default,
				ShaderElementData);
			return true;
		}

		FMeshPassProcessorRenderState DrawRenderState;
		FRHIUniformBuffer* PassUniformBuffer;
	};
}

void AddTempoLidarMediaTranslucencyPass(
	FRDGBuilder& GraphBuilder,
	const FTempoLidarMediaPassInputs& Inputs,
	const FSceneView& View,
	const FScene* Scene,
	FSceneUniformBuffer& SceneUniforms,
	TArrayView<const FTempoLidarMediaTranslucentBatch> Batches,
	FRDGTextureRef OpticalDepthProfile)
{
	if (Batches.IsEmpty() || !OpticalDepthProfile || !Inputs.SceneDepth || Inputs.ViewRect.IsEmpty() || !Inputs.ViewUniformBuffer.IsValid())
	{
		return;
	}
	const FIntVector ProfileSize = OpticalDepthProfile->Desc.GetSize();
	if (ProfileSize.X != Inputs.ViewRect.Width() || ProfileSize.Y != Inputs.ViewRect.Height())
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TempoLidarMediaTranslucency");

	FTempoLidarMediaTranslucencyPassUniformParameters* PassUniformParameters = GraphBuilder.AllocParameters<FTempoLidarMediaTranslucencyPassUniformParameters>();
	PassUniformParameters->OpticalDepthProfile = GraphBuilder.CreateUAV(OpticalDepthProfile);
	PassUniformParameters->ViewRectMin = Inputs.ViewRect.Min;
	// The same bin geometry the profile and resolve passes use; see MakeBinInputs there.
	PassUniformParameters->NumBins = ProfileSize.Z;
	PassUniformParameters->MaxRange = Inputs.Sensor.MaxRange;
	PassUniformParameters->FirstBinEdge = FMath::Clamp(Inputs.Sensor.FirstBinEdge, 1.0f, Inputs.Sensor.MaxRange * 0.5f);

	FTempoLidarMediaTranslucencyPassParameters* PassParameters = GraphBuilder.AllocParameters<FTempoLidarMediaTranslucencyPassParameters>();
	PassParameters->View = Inputs.ViewUniformBuffer;
	PassParameters->Scene = SceneUniforms.GetBuffer(GraphBuilder);
	PassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	PassParameters->SceneTextures = CreateSceneTextureUniformBuffer(GraphBuilder, View, ESceneTextureSetupMode::SceneDepth);
	PassParameters->Pass = GraphBuilder.CreateUniformBuffer(PassUniformParameters);
	// Fragments behind the opaque scene never reach the sensor. Read-only: the scene depth is
	// also what the profile and resolve passes read.
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Inputs.SceneDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);

	// Copied: the caller's view may be a temporary.
	TArray<FTempoLidarMediaTranslucentBatch>& PassBatches = *GraphBuilder.AllocObject<TArray<FTempoLidarMediaTranslucentBatch>>(Batches);
	const FIntRect ViewRect = Inputs.ViewRect;
	const ERHIFeatureLevel::Type FeatureLevel = Inputs.FeatureLevel;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("TempoLidarMediaTranslucency %d batches", Batches.Num()),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, &View, Scene, &PassBatches, ViewRect, FeatureLevel](FRHICommandList& RHICmdList)
	{
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		// The pass uniform buffer's RHI object only exists once the pass executes, so the draw
		// commands are built here rather than in a setup task.
		FRHIUniformBuffer* PassUniformBuffer = PassParameters->Pass->GetRHI();
		DrawDynamicMeshPass(View, RHICmdList, [&](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FTempoLidarMediaTranslucencyMeshProcessor Processor(Scene, FeatureLevel, &View, DynamicMeshPassContext, PassUniformBuffer);
			for (const FTempoLidarMediaTranslucentBatch& Batch : PassBatches)
			{
				if (Batch.Mesh && Batch.Proxy)
				{
					Processor.AddMeshBatch(*Batch.Mesh, Batch.BatchElementMask, Batch.Proxy, Batch.StaticMeshId);
				}
			}
		});
	});
}
