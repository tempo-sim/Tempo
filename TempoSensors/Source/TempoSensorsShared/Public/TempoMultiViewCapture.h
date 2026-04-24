// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Math/IntRect.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

class FSceneInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

// Consolidates N scene-capture tiles into one FSceneViewFamily rendered by a single FSceneRenderer
// into an atlas render target. Each view gets its own ViewRect inside the atlas; each tile
// contributes its own FSceneViewState (TAA/AE history), hide/show lists, and post-process settings.
//
// This replaces the per-tile FSceneRenderer path. Benefits come from sharing directional-light
// CSMs and fixed per-renderer setup cost (scene uniform buffer, Lumen/RT wire-up, GPU scene
// update) across tiles. Raster/post cost scales with pixels rendered, which is unchanged.
namespace TempoMultiViewCapture
{
	struct FViewSetup
	{
		// The tile's scene-capture component — owns view state (GetViewState(0)), post-process
		// settings, hide/show lists, view-owner pointer, and (via GetCameraView) first-person params.
		// Must be valid.
		USceneCaptureComponent2D* Component = nullptr;

		// World-space view origin (usually the camera rig position; all tiles share it).
		FVector ViewLocation = FVector::ZeroVector;

		// View rotation matrix in Unreal's capture convention — i.e. the same matrix
		// FScene::UpdateSceneCaptureContents would compute from the component's transform
		// (caller is responsible for applying the x=z,y=x,z=y axis swap).
		FMatrix ViewRotationMatrix = FMatrix::Identity;

		// Perspective projection matrix matching the tile's own capture size + FOV +
		// near-clip plane (built via BuildProjectionMatrix on the tile's TileOutputSizeXY).
		FMatrix ProjectionMatrix = FMatrix::Identity;

		// Destination rect inside the atlas (AtlasRT). One view renders into this rect.
		FIntRect ViewRect = FIntRect();

		// Tile perspective FOV, stored so post-process can depth-correct bokeh / DOF / etc.
		float FOV = 90.0f;
	};

	// Build one FSceneViewFamily containing all views, create one FSceneRenderer via
	// ISceneRenderBuilder, and Execute it. Synchronously enqueues the render commands before
	// returning; subsequent render commands (Canvas passes, readback) run after the family
	// render on the render thread.
	//
	// PrimaryComponent supplies family-wide settings: ShowFlags, CaptureSource, view extensions.
	// Its properties should be representative of the rig; tile-specific state (view state,
	// post-process, hide/show, clip-plane) comes from each FViewSetup.Component.
	TEMPOSENSORSSHARED_API void RenderTiles(
		FSceneInterface* Scene,
		USceneCaptureComponent2D* PrimaryComponent,
		UTextureRenderTarget2D* AtlasRT,
		TArrayView<const FViewSetup> Views);
}
