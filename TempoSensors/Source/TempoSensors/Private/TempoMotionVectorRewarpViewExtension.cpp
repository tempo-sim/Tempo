// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMotionVectorRewarpViewExtension.h"

#include "TempoMotionVectorRewarp.h"
#include "TempoMultiViewCapture.h"
#include "TempoSensors.h"

#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SceneViewExtensionContext.h"

FTempoMotionVectorRewarpViewExtension::FTempoMotionVectorRewarpViewExtension(const FAutoRegister& AutoRegister, FSceneInterface* InScene)
	: FSceneViewExtensionBase(AutoRegister)
	, Scene(InScene)
{
	// Read-only CVar: fixed for the life of the process.
	if (const IConsoleVariable* VelocityOutputPass = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VelocityOutputPass")))
	{
		if (VelocityOutputPass->GetInt() == 2)
		{
			bSupported = false;
			UE_LOG(LogTempoSensors, Warning, TEXT("r.VelocityOutputPass=2 renders velocity after the base pass; motion vector rewarp is disabled. Use 0 or 1."));
		}
	}
}

float FTempoMotionVectorRewarpViewExtension::ComputeExtrapolationFactor(double NowSeconds, double LastCaptureSeconds, double TickDeltaSeconds)
{
	if (LastCaptureSeconds < 0.0 || TickDeltaSeconds <= UE_DOUBLE_SMALL_NUMBER)
	{
		return 1.0f;
	}

	// Captures happen on tick boundaries, so anything short of a second tick means the view
	// rendered last tick and its vectors already span the right interval. The tolerance absorbs
	// float rounding in per-tick deltas (a fixed step's delta is recomputed every tick), which
	// would otherwise turn an exact one-tick interval into a factor a hair above one.
	const double Elapsed = NowSeconds - LastCaptureSeconds;
	if (Elapsed <= TickDeltaSeconds * (1.0 + 1e-3))
	{
		return 1.0f;
	}
	return static_cast<float>(Elapsed / TickDeltaSeconds);
}

void FTempoMotionVectorRewarpViewExtension::SetExtrapolationFactor(const FSceneViewStateInterface* ViewState, float Factor)
{
	check(IsInGameThread());
	if (!ViewState)
	{
		return;
	}

	TSharedRef<FTempoMotionVectorRewarpViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FTempoMotionVectorRewarpViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(TempoMotionVectorRewarpSetFactor)([Self, ViewState, Factor](FRHICommandListImmediate&)
	{
		Self->ExtrapolationFactors_RenderThread.Add(ViewState, Factor);
	});
}

void FTempoMotionVectorRewarpViewExtension::RemoveViewState(const FSceneViewStateInterface* ViewState)
{
	check(IsInGameThread());
	if (!ViewState)
	{
		return;
	}

	TSharedRef<FTempoMotionVectorRewarpViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FTempoMotionVectorRewarpViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(TempoMotionVectorRewarpRemoveViewState)([Self, ViewState](FRHICommandListImmediate&)
	{
		Self->ExtrapolationFactors_RenderThread.Remove(ViewState);
	});
}

bool FTempoMotionVectorRewarpViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Scene captures gather with a scene-only context; viewports pass a viewport.
	return bSupported && bActive && Context.Viewport == nullptr && Context.Scene == Scene;
}

void FTempoMotionVectorRewarpViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	const float* Factor = ExtrapolationFactors_RenderThread.Find(InView.State);
	if (!Factor || *Factor <= 1.0f)
	{
		return;
	}

	// The uniform buffer handed to this hook doesn't expose the velocity texture yet (the scene
	// texture setup mode only gains it after the base pass), so go to the family's scene textures.
	FRDGTextureRef Velocity = nullptr;
	FRDGTextureRef SceneDepth = nullptr;
	FIntRect ViewRect;
	if (!TempoMultiViewCapture::GetRenderedViewSceneTextures(InView, Velocity, SceneDepth, ViewRect))
	{
		return;
	}

	// Nothing wrote velocity this frame (no primitive moved): every pixel reprojects from depth.
	if (!Velocity->HasBeenProduced())
	{
		return;
	}

	AddTempoMotionVectorRewarpPass(GraphBuilder, InView.GetFeatureLevel(), InView.ViewUniformBuffer, Velocity, SceneDepth, ViewRect, *Factor);
}
