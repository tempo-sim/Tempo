// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Math/IntRect.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "SceneTypes.h"
#include "ShowFlags.h"

class FSceneInterface;
class FSceneViewStateInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

// Consolidates N views into one FSceneViewFamily rendered by a single FSceneRenderer into an atlas
// render target. Each view gets its own ViewRect inside the atlas; each view contributes its own
// FSceneViewState (TAA/AE history) and post-process settings. Views may come from different capture
// components: everything the engine treats per view (hide/show lists, view owner, far clip, LOD,
// clip plane, lighting channels) is taken from the view's own Component, while family-level
// settings (show flags, capture source, composite mode, view extensions) come from the
// PrimaryComponent passed to RenderTiles.
namespace TempoMultiViewCapture
{
	struct FViewSetup
	{
		// The capture component this view belongs to. Required. Supplies the per-view owner-level
		// settings listed above; several views may share one component (the tiles of one sensor).
		USceneCaptureComponent2D* Component = nullptr;

		// Per-view scene view state (TAA history). Must be valid per view so that tiles don't share
		// history.
		FSceneViewStateInterface* ViewState = nullptr;

		// View state whose eye-adaptation buffer this view reads and writes. Null means the view's
		// own ViewState. The tiles of one sensor point at that sensor's first tile so they cannot
		// drift apart in brightness; views from different sensors in one family never share, so each
		// sensor meters its own scene.
		FSceneViewStateInterface* ExposureViewState = nullptr;

		// Per-view post-process settings (holds the distortion PPM blendable + AE bias).
		// Pointer aliases a caller-owned FPostProcessSettings; must outlive the RenderTiles call.
		const FPostProcessSettings* PostProcessSettings = nullptr;
		float PostProcessBlendWeight = 1.0f;

		// One-shot per-view camera-cut flag. Consumed by the helper; caller is responsible for
		// clearing/setting on each call.
		bool bCameraCut = false;

		// World-space view origin (usually the sensor position; the tiles of one sensor share it).
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

		// Whether this view may use ray tracing at all (Component->bUseRayTracingIfEnabled still has
		// to be set). False for a view whose scene content is discarded, such as the camera's proxy
		// tonemap view, so the family builds no ray tracing scene for it.
		bool bAllowRayTracing = true;
	};

	// Build one FSceneViewFamily containing all views, create one FSceneRenderer via
	// ISceneRenderBuilder, and Execute it. Synchronously enqueues the render commands before
	// returning; subsequent render commands run after the family render on the render thread.
	//
	// PrimaryComponent supplies family-wide settings: view extensions, realtime-update flag,
	// composite mode, and (unless ShowFlagsOverride is given) the show flags. CaptureSource is
	// passed explicitly because the primary's own capture may use a different source than the atlas.
	//
	// ShowFlagsOverride, when non-null, replaces PrimaryComponent->ShowFlags for the family. Sensors
	// that render their tiles with a trimmed flag set (the multi-tile camera turns off bloom, motion
	// blur and friends) pass the trimmed copy here instead of mutating the component's flags.
	//
	// DebugName labels the render in traces (the "SceneRender" breadcrumb and the render graph
	// event).
	//
	// ResolutionFraction is the GlobalResolutionFraction handed to the family's
	// FLegacyScreenPercentageDriver. 1.0 = no upscaling (each tile rasterizes at its full
	// ViewRect). <1.0 rasterizes at fraction*ViewRect and upsamples to ViewRect — TSR/TAAU when
	// the tile's AA method is TSR/TemporalAA (FSceneView constructor auto-promotes
	// PrimaryScreenPercentageMethod to TemporalUpscale in that case), spatial otherwise.
	TEMPOSENSORS_API void RenderTiles(
		FSceneInterface* Scene,
		USceneCaptureComponent2D* PrimaryComponent,
		UTextureRenderTarget2D* AtlasRT,
		TArrayView<const FViewSetup> Views,
		ESceneCaptureSource CaptureSource,
		float ResolutionFraction = 1.0f,
		const FEngineShowFlags* ShowFlagsOverride = nullptr,
		const FString& DebugName = TEXT("TempoTilesMultiView"));
}
