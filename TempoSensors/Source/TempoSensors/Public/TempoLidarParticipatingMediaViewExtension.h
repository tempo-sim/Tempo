// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "TempoLidarParticipatingMedia.h"

class FSceneInterface;

// What one lidar capture asks of the participating media passes.
struct FTempoLidarMediaCaptureSetup
{
	// Size of the results texture: the lidar's packed atlas, so every tile's view rect indexes it.
	FIntPoint ResultsSize = FIntPoint::ZeroValue;
	FTempoLidarMediaSensorInputs Sensor;
};

// Runs the lidar's participating media passes (see TempoLidarParticipatingMedia.h) on every view of
// the lidar's tile family and keeps their results in a texture the lidar reads back next to its
// atlas.
//
// One extension is owned per lidar. It is gathered into a view family only while the lidar marks it
// active around its own tile render, so other captures in the same scene are unaffected. The setup
// for a capture is sent ahead of the render with a render command, so it applies to exactly that
// render.
class TEMPOSENSORS_API FTempoLidarParticipatingMediaViewExtension : public FSceneViewExtensionBase
{
public:
	FTempoLidarParticipatingMediaViewExtension(const FAutoRegister& AutoRegister, FSceneInterface* InScene);

	// Game thread. The setup for the next render; ordered with the render commands that follow.
	void SetCaptureSetup(const FTempoLidarMediaCaptureSetup& Setup);

	// Game thread. Whether the extension may be gathered into view families right now. Set around
	// the render it should act on.
	void SetActive(bool bInActive) { bActive = bInActive; }

	// Game thread. Release the results texture on the render thread. Called before the owner drops
	// its reference, so no RHI resource is freed from the game thread.
	void ReleaseResources();

	// Render thread. Copy the results of the last render into a staging texture of the same size and
	// format, for readback. Does nothing, leaving the staging texture untouched, if there are none.
	void CopyResultsToStaging_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* Staging) const;

	// Begin ISceneViewExtension
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	// End ISceneViewExtension

	static constexpr EPixelFormat ResultsFormat = PF_R16G16B16A16_UINT;

private:
	// Render thread. Make sure the results texture matches the setup's size.
	void EnsureResultsTexture_RenderThread(FRHICommandListBase& RHICmdList);

	FSceneInterface* Scene = nullptr;

	// Game thread.
	bool bActive = false;

	// Render thread only.
	FTempoLidarMediaCaptureSetup Setup_RenderThread;
	FTextureRHIRef Results_RenderThread;
};
