// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "TempoLidarParticipatingMedia.h"

class FSceneInterface;

// A height fog component and the label its fog's medium echoes carry, its actor's.
struct FTempoLidarMediaLabeledHeightFog
{
	// The scene's id for the component, uint64(Component), which is how the renderer keys it.
	uint64 Id = 0;
	uint32 Label = 0;
};

// A local fog volume and the label its medium echoes carry, its actor's.
struct FTempoLidarMediaLabeledVolume
{
	FVector WorldPosition = FVector::ZeroVector;
	uint32 Label = 0;
};

// What one lidar capture asks of the participating media passes.
struct FTempoLidarMediaCaptureSetup
{
	// Size of the results texture: the lidar's packed atlas, so every tile's view rect indexes it.
	FIntPoint ResultsSize = FIntPoint::ZeroValue;
	// Passed through to the passes, except FogLabel, which is resolved from LabeledHeightFogs.
	FTempoLidarMediaSensorInputs Sensor;
	// Also rasterize the view's translucent primitives into the profile.
	bool bIncludeTranslucency = true;
	// Every height fog in the scene with its label. The height fog and the volumetric fog grid carry
	// the label of the one the renderer composes, the first registered, which the passes pick out
	// of these by id. Empty, or none of them the renderer's, they carry no label.
	TArray<FTempoLidarMediaLabeledHeightFog> LabeledHeightFogs;
	// Every local fog volume in the scene with its label; the passes match the instances the view
	// composes to these by position. Empty, they all carry the height fog's label.
	TArray<FTempoLidarMediaLabeledVolume> LabeledFogVolumes;
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

	// Game thread. The setup for the next render; ordered with the render commands that follow. Also
	// sizes the results texture to it and clears every pixel to "no media", so a tile the passes
	// skip, or a pixel none of them write, reads as it would without media.
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

	static constexpr EPixelFormat ResultsFormat = FTempoLidarMediaPixel::Format;

private:
	// Render thread. Make sure the results texture matches the setup's size.
	void EnsureResultsTexture_RenderThread(FRHICommandListBase& RHICmdList);

	// Render thread. Clear every pixel of the results texture to "no media".
	void ClearResults_RenderThread(FRHICommandListImmediate& RHICmdList);

	FSceneInterface* Scene = nullptr;

	// Game thread.
	bool bActive = false;

	// Render thread only.
	FTempoLidarMediaCaptureSetup Setup_RenderThread;
	FTextureRHIRef Results_RenderThread;
};
