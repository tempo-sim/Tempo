// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCustomDepthSkipViewExtension.h"

#include "TempoMultiViewCapture.h"

#include "SceneView.h"
#include "SceneViewExtensionContext.h"
#include "RenderingThread.h"

FTempoCustomDepthSkipViewExtension::FTempoCustomDepthSkipViewExtension(const FAutoRegister& AutoRegister, FSceneInterface* InScene)
	: FSceneViewExtensionBase(AutoRegister)
	, Scene(InScene)
{
}

bool FTempoCustomDepthSkipViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Scene captures gather with a scene-only context; viewports pass a viewport.
	return bActive && Context.Viewport == nullptr && Context.Scene == Scene;
}

void FTempoCustomDepthSkipViewExtension::SetViewStateFilter(TArray<const FSceneViewStateInterface*>&& ViewStates)
{
	check(IsInGameThread());
	if (ViewStates == ViewStateFilter)
	{
		return;
	}
	ViewStateFilter = ViewStates;
	ENQUEUE_RENDER_COMMAND(TempoCustomDepthSkipSetFilter)(
		[this, ViewStates = MoveTemp(ViewStates)](FRHICommandListImmediate&) mutable
		{
			ViewStateFilter_RenderThread = MoveTemp(ViewStates);
		});
}

void FTempoCustomDepthSkipViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	// Before visibility: the renderer has not decided about the pass yet, so only remember the view.
	if (ViewStateFilter_RenderThread.IsEmpty() || ViewStateFilter_RenderThread.Contains(InView.State))
	{
		Views_RenderThread.Add(&InView);
	}
}

void FTempoCustomDepthSkipViewExtension::PreRenderBasePass_RenderThread(FRDGBuilder& GraphBuilder, bool bDepthBufferIsPopulated)
{
	// After visibility and before either custom depth pass location.
	for (FSceneView* View : Views_RenderThread)
	{
		TempoMultiViewCapture::SkipCustomDepthPass(*View);
	}
	Views_RenderThread.Reset();
}

void FTempoCustomDepthSkipViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// A renderer that never reached the base pass leaves the views behind; they die with it.
	Views_RenderThread.Reset();
}
