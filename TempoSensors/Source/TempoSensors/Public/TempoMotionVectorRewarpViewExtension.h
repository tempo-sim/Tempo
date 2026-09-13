// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FSceneInterface;
class FSceneViewStateInterface;

// Corrects the motion vectors of views that render less often than the scene ticks.
//
// Per-primitive previous transforms advance once per engine tick (FScene::StartFrame, called from
// the engine loop for every scene whether or not anything renders), so a view rendering every N
// ticks sees object velocities that span one tick while its previous view matrices, which live on
// its view state, span all N. TSR and Lumen then reproject moving objects N-1 ticks short and ghost.
// This extension rescales the object-motion part of each such view's velocity texture by N right
// after the base pass, before Lumen's temporal passes and TSR consume it. The camera part is left as
// is; see TempoMotionVectorRewarp.usf.
//
// One extension is owned per camera. It is gathered into a view family only while the camera
// marks it active around its own tile render, and it acts only on views whose view state has been
// given a factor, so other captures in the same scene are unaffected.
//
// The velocity texture must be complete by the end of the base pass, which holds for
// r.VelocityOutputPass 0 (depth pass, the default) and 1 (base pass). With 2 (after the base pass)
// the extension disables itself and logs a warning.
class TEMPOSENSORS_API FTempoMotionVectorRewarpViewExtension : public FSceneViewExtensionBase
{
public:
	FTempoMotionVectorRewarpViewExtension(const FAutoRegister& AutoRegister, FSceneInterface* InScene);

	// Game thread. The factor to apply on the next render of ViewState: the number of scene ticks
	// since that view state last rendered. Values of one or less leave the view untouched. Ordered
	// with the render commands that follow, so it applies to exactly the next render.
	void SetExtrapolationFactor(const FSceneViewStateInterface* ViewState, float Factor);

	// Game thread. Forget a view state that is about to be destroyed.
	void RemoveViewState(const FSceneViewStateInterface* ViewState);

	// Game thread. Whether the extension may be gathered into view families right now. Set around
	// the render that should be rewarped.
	void SetActive(bool bInActive) { bActive = bInActive; }

	// Ticks since the last render, from world time: (Now - LastCapture) / TickDelta. Returns one
	// (no rewarp) when there is no previous capture, the tick delta is not positive, or less than
	// one tick has elapsed.
	static float ComputeExtrapolationFactor(double NowSeconds, double LastCaptureSeconds, double TickDeltaSeconds);

	// Begin ISceneViewExtension
	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	// End ISceneViewExtension

private:
	FSceneInterface* Scene = nullptr;

	// Game thread.
	bool bActive = false;
	bool bSupported = true;

	// Render thread only.
	TMap<const FSceneViewStateInterface*, float> ExtrapolationFactors_RenderThread;
};
