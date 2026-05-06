// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Math/IntRect.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "SceneTypes.h"

class FSceneInterface;
class FSceneViewStateInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

// Consolidates N views into one FSceneViewFamily rendered by a single FSceneRenderer into an atlas
// render target. Each view gets its own ViewRect inside the atlas; each view contributes its own
// FSceneViewState (TAA/AE history) and post-process settings. Owner-level context (show flags,
// hide/show lists, view owner, LOD, ray-tracing) is taken from the PrimaryComponent passed to
// RenderTiles — tiles no longer need to be USceneCaptureComponent2D instances.
namespace TempoMultiViewCapture
{
	struct FViewSetup
	{
		// Per-view scene view state (TAA / auto-exposure history). Must be valid per view so that
		// tiles don't share history.
		FSceneViewStateInterface* ViewState = nullptr;

		// Per-view post-process settings (holds the distortion PPM blendable + AE bias).
		// Pointer aliases a caller-owned FPostProcessSettings; must outlive the RenderTiles call.
		const FPostProcessSettings* PostProcessSettings = nullptr;
		float PostProcessBlendWeight = 1.0f;

		// One-shot per-view camera-cut flag. Consumed by the helper; caller is responsible for
		// clearing/setting on each call.
		bool bCameraCut = false;

		// World-space view origin (usually the camera rig position; all tiles share it).
		FVector ViewLocation = FVector::ZeroVector;

		// View rotation matrix in Unreal's capture convention — i.e. the same matrix
		// FScene::UpdateSceneCaptureContents would compute from a component's transform
		// (caller is responsible for applying the x=z,y=x,z=y axis swap).
		FMatrix ViewRotationMatrix = FMatrix::Identity;

		// Perspective projection matrix matching the tile's render size + FOV + near-clip.
		FMatrix ProjectionMatrix = FMatrix::Identity;

		// Destination rect inside the atlas (AtlasRT). One view renders into this rect.
		FIntRect ViewRect = FIntRect();

		// Tile perspective FOV, stored for post-process depth-correction.
		float FOV = 90.0f;
	};

	// Build one FSceneViewFamily containing all views, create one FSceneRenderer via
	// ISceneRenderBuilder, and Execute it. Synchronously enqueues the render commands before
	// returning; subsequent render commands run after the family render on the render thread.
	//
	// PrimaryComponent supplies family-wide settings: ShowFlags, view extensions, hide/show lists,
	// view owner, LOD, ray-tracing flag, clip plane, lighting channels. CaptureSource is passed
	// explicitly because the primary's own capture may use a different source than the atlas.
	//
	// ResolutionFraction is the GlobalResolutionFraction handed to the family's
	// FLegacyScreenPercentageDriver. 1.0 = no upscaling (each tile rasterizes at its full
	// ViewRect). <1.0 rasterizes at fraction*ViewRect and upsamples to ViewRect — TSR/TAAU when
	// the tile's AA method is TSR/TemporalAA (FSceneView constructor auto-promotes
	// PrimaryScreenPercentageMethod to TemporalUpscale in that case), spatial otherwise.
	TEMPOSENSORSSHARED_API void RenderTiles(
		FSceneInterface* Scene,
		USceneCaptureComponent2D* PrimaryComponent,
		UTextureRenderTarget2D* AtlasRT,
		TArrayView<const FViewSetup> Views,
		ESceneCaptureSource CaptureSource,
		float ResolutionFraction = 1.0f);
}
