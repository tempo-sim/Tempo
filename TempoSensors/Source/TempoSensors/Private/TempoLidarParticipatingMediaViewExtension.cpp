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
	ENQUEUE_RENDER_COMMAND(TempoLidarMediaSetCaptureSetup)([Self, Setup](FRHICommandListImmediate&)
	{
		Self->Setup_RenderThread = Setup;
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

void FTempoLidarParticipatingMediaViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
	FTempoLidarMediaPassInputs PassInputs;
	if (!TempoMultiViewCapture::GetViewParticipatingMediaInputs(InView, PassInputs))
	{
		return;
	}
	PassInputs.Sensor = Setup_RenderThread.Sensor;

	EnsureResultsTexture_RenderThread(GraphBuilder.RHICmdList);
	if (!Results_RenderThread.IsValid())
	{
		return;
	}
	// Every tile of the family writes its own rect of the one results texture.
	if (PassInputs.ViewRect.Max.X > Results_RenderThread->GetSizeX() || PassInputs.ViewRect.Max.Y > Results_RenderThread->GetSizeY())
	{
		UE_LOG(LogTempoSensors, Warning, TEXT("Lidar media: view rect (%d,%d)-(%d,%d) exceeds the results texture (%dx%d). Skipping."),
			PassInputs.ViewRect.Min.X, PassInputs.ViewRect.Min.Y, PassInputs.ViewRect.Max.X, PassInputs.ViewRect.Max.Y,
			Results_RenderThread->GetSizeX(), Results_RenderThread->GetSizeY());
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TempoLidarParticipatingMedia");

	FRDGTextureRef Results = RegisterExternalTexture(GraphBuilder, Results_RenderThread, TEXT("TempoLidarMedia.Results"));

	FRDGTextureRef Profile = AddTempoLidarMediaProfilePass(GraphBuilder, PassInputs);
	if (!Profile)
	{
		return;
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
