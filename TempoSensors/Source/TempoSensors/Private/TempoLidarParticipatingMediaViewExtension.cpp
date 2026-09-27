// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidarParticipatingMediaViewExtension.h"

#include "TempoMultiViewCapture.h"
#include "TempoSensors.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SceneViewExtensionContext.h"

FTempoLidarParticipatingMediaViewExtension::FTempoLidarParticipatingMediaViewExtension(const FAutoRegister& AutoRegister, FSceneInterface* InScene)
	: FSceneViewExtensionBase(AutoRegister)
	, Scene(InScene)
{
}

void FTempoLidarParticipatingMediaViewExtension::SetCaptureSetup(const FTempoLidarMediaCaptureSetup& Setup)
{
	check(IsInGameThread());

	TSharedRef<FTempoLidarParticipatingMediaViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FTempoLidarParticipatingMediaViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(TempoLidarMediaSetCaptureSetup)([Self, Setup](FRHICommandListImmediate& RHICmdList)
	{
		Self->Setup_RenderThread = Setup;
		// Every pixel starts the capture as "no media", so a tile the passes skip, or one the
		// texture was just created for, reports its beams as they would be without media rather
		// than as whatever the memory held.
		Self->EnsureResultsTexture_RenderThread(RHICmdList);
		Self->ClearResults_RenderThread(RHICmdList);
	});
}

void FTempoLidarParticipatingMediaViewExtension::ReleaseResources()
{
	check(IsInGameThread());

	TSharedRef<FTempoLidarParticipatingMediaViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FTempoLidarParticipatingMediaViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(TempoLidarMediaReleaseResources)([Self](FRHICommandListImmediate&)
	{
		Self->Results_RenderThread.SafeRelease();
		Self->Setup_RenderThread = FTempoLidarMediaCaptureSetup();
	});
}

bool FTempoLidarParticipatingMediaViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Scene captures gather with a scene-only context; viewports pass a viewport.
	return bActive && Context.Viewport == nullptr && Context.Scene == Scene;
}

void FTempoLidarParticipatingMediaViewExtension::EnsureResultsTexture_RenderThread(FRHICommandListBase& RHICmdList)
{
	const FIntPoint Size = Setup_RenderThread.ResultsSize;
	if (Results_RenderThread.IsValid() && Results_RenderThread->GetSizeXY() == Size)
	{
		return;
	}
	Results_RenderThread.SafeRelease();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return;
	}

	const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("TempoLidarMediaResults"), Size, ResultsFormat)
		.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
	Results_RenderThread = RHICmdList.CreateTexture(Desc);
#else
	Results_RenderThread = RHICreateTexture(Desc);
#endif
}

void FTempoLidarParticipatingMediaViewExtension::ClearResults_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());
	if (!Results_RenderThread.IsValid())
	{
		return;
	}
	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TempoLidarMediaClear"));
	const FRDGTextureRef Results = RegisterExternalTexture(GraphBuilder, Results_RenderThread, TEXT("TempoLidarMedia.Results"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Results), FTempoLidarMediaPixel::NoMediaClearValue());
	GraphBuilder.Execute();
}

void FTempoLidarParticipatingMediaViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
	FTempoLidarMediaPassInputs PassInputs;
	if (!TempoMultiViewCapture::GetViewParticipatingMediaInputs(InView, PassInputs))
	{
		return;
	}
	PassInputs.Sensor = Setup_RenderThread.Sensor;
	// The height fog the renderer composes is the first registered in the scene, which need not be
	// the first the game thread would find, so its label is looked up by the scene's id for it.
	PassInputs.Sensor.FogLabel = 0;
	for (const FTempoLidarMediaLabeledHeightFog& HeightFog : Setup_RenderThread.LabeledHeightFogs)
	{
		if (HeightFog.Id != 0 && HeightFog.Id == PassInputs.Fog.HeightFogId)
		{
			PassInputs.Sensor.FogLabel = HeightFog.Label;
			break;
		}
	}
	// The renderer uploads local fog volume instances in the view's translated world space, so the
	// labeled volumes are matched to them there.
	const FVector PreViewTranslation = InView.ViewMatrices.GetPreViewTranslation();
	PassInputs.Fog.LabeledVolumes.Reserve(Setup_RenderThread.LabeledFogVolumes.Num());
	for (const FTempoLidarMediaLabeledVolume& Volume : Setup_RenderThread.LabeledFogVolumes)
	{
		PassInputs.Fog.LabeledVolumes.Add(FVector4f(FVector3f(PreViewTranslation + Volume.WorldPosition), static_cast<float>(Volume.Label & 0xFFu)));
	}

	// Sized to the setup and cleared to "no media" ahead of this render by SetCaptureSetup.
	if (!Results_RenderThread.IsValid())
	{
		return;
	}
	// Every tile of the family writes its own slot of the one results texture: the rect its output
	// lands in, not the rect the renderer rasterized it at, which is quantized and can differ.
	const FIntRect& OutputRect = PassInputs.OutputRect;
	if (OutputRect.Max.X > Results_RenderThread->GetSizeX() || OutputRect.Max.Y > Results_RenderThread->GetSizeY()
		|| OutputRect.Size() != PassInputs.ViewRect.Size())
	{
		UE_LOG(LogTempoSensors, Warning, TEXT("Lidar media: output rect (%d,%d)-(%d,%d) does not fit the results texture (%dx%d) or its view rect (%dx%d). Skipping."),
			OutputRect.Min.X, OutputRect.Min.Y, OutputRect.Max.X, OutputRect.Max.Y,
			Results_RenderThread->GetSizeX(), Results_RenderThread->GetSizeY(),
			PassInputs.ViewRect.Width(), PassInputs.ViewRect.Height());
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TempoLidarParticipatingMedia");

	FRDGTextureRef Results = RegisterExternalTexture(GraphBuilder, Results_RenderThread, TEXT("TempoLidarMedia.Results"));

	const FTempoLidarMediaProfile Profile = AddTempoLidarMediaProfilePass(GraphBuilder, PassInputs);
	if (!Profile.IsValid())
	{
		return;
	}
	if (Setup_RenderThread.bIncludeTranslucency)
	{
		TArray<FTempoLidarMediaTranslucentBatch> Batches;
		const FScene* RenderScene = nullptr;
		FSceneUniformBuffer* SceneUniforms = nullptr;
		if (TempoMultiViewCapture::GetViewTranslucentBatches(InView, Batches, RenderScene, SceneUniforms) && !Batches.IsEmpty())
		{
			AddTempoLidarMediaTranslucencyPass(GraphBuilder, PassInputs, InView, RenderScene, *SceneUniforms, Batches, Profile);
		}
	}
	AddTempoLidarMediaResolvePass(GraphBuilder, PassInputs, Profile, Results);

	// The lidar copies the results to a staging texture with a plain RHI command after the graph.
	GraphBuilder.SetTextureAccessFinal(Results, ERHIAccess::CopySrc);
}

void FTempoLidarParticipatingMediaViewExtension::CopyResultsToStaging_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* Staging) const
{
	check(IsInRenderingThread());
	if (!Results_RenderThread.IsValid() || !Staging)
	{
		return;
	}
	if (Results_RenderThread->GetSizeXY() != Staging->GetSizeXY() || Results_RenderThread->GetFormat() != Staging->GetFormat())
	{
		UE_LOG(LogTempoSensors, Warning, TEXT("Lidar media: results texture (%dx%d, %s) does not match its staging texture (%dx%d, %s). Skipping copy."),
			Results_RenderThread->GetSizeX(), Results_RenderThread->GetSizeY(), GPixelFormats[Results_RenderThread->GetFormat()].Name,
			Staging->GetSizeX(), Staging->GetSizeY(), GPixelFormats[Staging->GetFormat()].Name);
		return;
	}
	RHICmdList.Transition(FRHITransitionInfo(Results_RenderThread, ERHIAccess::Unknown, ERHIAccess::CopySrc));
	RHICmdList.CopyTexture(Results_RenderThread, Staging, FRHICopyTextureInfo());
}
