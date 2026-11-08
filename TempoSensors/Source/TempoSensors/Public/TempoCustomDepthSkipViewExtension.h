// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FSceneInterface;

// Skips the custom depth pass of the captures rendered while it is active.
//
// The custom depth pass renders every labeled primitive a second time, into the depth/stencil pair
// the camera's distortion material packs the label from. A capture whose output needs no label
// (no label or bounding box request is pending, or the proxy render, whose material replaces scene
// color without reading the stencil) pays for that pass for nothing. The renderer decides per
// view whether to run it from the primitives the view saw; this extension clears that decision
// after visibility and before the pass, so the pass is skipped and the stencil the material
// samples is the renderer's cleared fallback. Nothing in the color path reads it.
//
// One extension is owned per camera. It is gathered into a view family only while the camera
// marks it active around a render, so other captures in the same scene are unaffected.
class TEMPOSENSORS_API FTempoCustomDepthSkipViewExtension : public FSceneViewExtensionBase
{
public:
	FTempoCustomDepthSkipViewExtension(const FAutoRegister& AutoRegister, FSceneInterface* InScene);

	// Game thread. Whether the extension may be gathered into view families right now. Set around
	// the render whose custom depth pass should be skipped.
	void SetActive(bool bInActive) { bActive = bInActive; }

	// Begin ISceneViewExtension
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	virtual void PreRenderBasePass_RenderThread(FRDGBuilder& GraphBuilder, bool bDepthBufferIsPopulated) override;
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	// End ISceneViewExtension

private:
	FSceneInterface* Scene = nullptr;

	// Game thread.
	bool bActive = false;

	// Render thread only. The views of the family being rendered, collected before visibility and
	// marked after it.
	TArray<FSceneView*> Views_RenderThread;
};
