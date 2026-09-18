// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Math/IntRect.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "RenderGraphFwd.h"
#include "SceneTypes.h"
#include "TempoLidarParticipatingMedia.h"

class FScene;
class FSceneInterface;
class IConsoleVariable;
class FSceneUniformBuffer;
class FSceneView;
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
		// Per-view scene view state (TAA history). Must be valid per view so that tiles don't share
		// history. Exposure state is deliberately shared: every view is pointed at the first view's
		// state for eye adaptation.
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
	TEMPOSENSORS_API void RenderTiles(
		FSceneInterface* Scene,
		USceneCaptureComponent2D* PrimaryComponent,
		UTextureRenderTarget2D* AtlasRT,
		TArrayView<const FViewSetup> Views,
		ESceneCaptureSource CaptureSource,
		float ResolutionFraction = 1.0f);

	// Render thread. The scene velocity and resolved depth textures a view is being rendered into,
	// and the rect it occupies in them, for view extension hooks whose scene texture uniform buffer
	// doesn't yet expose what they need (the base pass hook's lacks velocity). Reaches into the
	// renderer's FViewFamilyInfo, hence lives here with the other engine-private mirrors. Returns
	// false if the family has no initialized scene textures or either texture is missing.
	TEMPOSENSORS_API bool GetRenderedViewSceneTextures(const FSceneView& View, FRDGTextureRef& OutVelocity, FRDGTextureRef& OutSceneDepth, FIntRect& OutViewRect);

	// Render thread. The inputs the lidar's participating media passes need from a view being
	// rendered: its rect and the family's resolved scene depth, and the fog the camera's fog pass
	// would compose for it, read from the renderer's per-view fog constants and resources (the
	// exponential height fog parameters, the volumetric fog froxel grid if one was rendered, and
	// the local fog volume data if local fog volumes are composed analytically). Returns false if
	// the family has no scene textures. Leaves Sensor untouched.
	TEMPOSENSORS_API bool GetViewParticipatingMediaInputs(const FSceneView& View, FTempoLidarMediaPassInputs& OutInputs);

	// Render thread. The translucent mesh batches visible in a view being rendered, dynamic (particle
	// systems and the like) and static, as the renderer gathered them for its own translucency pass,
	// plus the render scene and the scene uniforms a mesh pass over them needs. Batches whose
	// materials are not translucent are filtered by the pass; only the cheap relevance flags are
	// checked here. Returns false if the view has no render scene.
	TEMPOSENSORS_API bool GetViewTranslucentBatches(const FSceneView& View, TArray<FTempoLidarMediaTranslucentBatch>& OutBatches, const FScene*& OutScene, FSceneUniformBuffer*& OutSceneUniforms);

	// Game thread. Rescales the volumetric fog history blend for the renders issued while it lives.
	//
	// The engine blends each render's fog grid with the previous render's by a fixed weight
	// (r.VolumetricFog.HistoryWeight, 0.9 by default), which assumes a render every scene tick: the
	// history then decays over about ten ticks. A view that renders every N ticks blends once per N
	// ticks and so takes N times longer, in scene time, to converge; moving media trail behind their
	// emitters and a sensor reads a grid up to seconds old. Raising the weight to the Nth power gives
	// one blend the decay of N per-tick blends, so the grid converges per tick of scene time whatever
	// the view's rate, the same correction the motion vector rewarp applies to velocities.
	//
	// The variable is render-thread safe, so its changes are applied on the render thread in order
	// with the render commands enqueued between them: only the renders issued inside the scope see
	// the rescaled weight. Its set-by priority is kept, so scalability settings still own it. A
	// factor of one or less, or a weight of zero, changes nothing.
	class TEMPOSENSORS_API FScopedVolumetricFogHistoryRescale
	{
	public:
		explicit FScopedVolumetricFogHistoryRescale(float TicksSinceLastRender);
		~FScopedVolumetricFogHistoryRescale();

		FScopedVolumetricFogHistoryRescale(const FScopedVolumetricFogHistoryRescale&) = delete;
		FScopedVolumetricFogHistoryRescale& operator=(const FScopedVolumetricFogHistoryRescale&) = delete;

	private:
		IConsoleVariable* HistoryWeight = nullptr;
		float OriginalWeight = 0.0f;
	};
}
