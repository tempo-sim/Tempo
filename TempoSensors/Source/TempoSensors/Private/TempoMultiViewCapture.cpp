// Copyright Tempo Simulation, LLC. All Rights Reserved

// This helper mirrors engine-private logic in Renderer/Private/SceneCaptureRendering.cpp —
// specifically SetupViewFamilyForSceneCapture, SetupSceneViewExtensionsForSceneCapture,
// CreateSceneRendererForSceneCapture, and UpdateSceneCaptureContent_RenderThread.
// Locations: 666 / 806 / 824 / 415 in 5.7; 689 / 833 / 851 / 453 in 5.8.
//
// 5.8 re-diff: the engine refactored those functions' own signatures to flag enums
// (ESetupViewFamilyFlags / ESceneRendererCreationFlags replacing bool params) and moved
// RegisterExternalTexture out of UpdateSceneCaptureContent_RenderThread. None of that affects
// this mirror, which reimplements the bodies and drives the renderer via the public
// ISceneRenderBuilder interface. The one behavioral addition we carry over is the new per-view
// FSceneViewInitOptions::SkylightScale (sourced from the capture component); see below.
// Re-diff and update for any newer engine version.
#if !(ENGINE_MAJOR_VERSION == 5 && (ENGINE_MINOR_VERSION == 7 || ENGINE_MINOR_VERSION == 8))
#error "TempoMultiViewCapture is pinned to UE 5.7/5.8 engine internals. Re-diff and update for the new engine version."
#endif

#include "TempoMultiViewCapture.h"

#include "Components/SceneCaptureComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/BlendableInterface.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "LegacyScreenPercentageDriver.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneRenderBuilderInterface.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "TextureResource.h"

// Renderer-private — reachable via PrivateIncludePaths set in TempoSensorsShared.Build.cs.
// In 5.7 SceneRendering.h transitively includes RayTracing/RayTracingScene.h, which declares
// private members that TempoSceneCaptureComponent2D.cpp relies on accessing via a
// `#define private public` hack. UBT combines both .cpp files into one Unity translation unit;
// this file comes first alphabetically, so RayTracingScene.h gets parsed here. Without
// mirroring the define, the class is parsed with real `private` — the later hack in
// TempoSceneCaptureComponent2D.cpp is then a no-op because `#pragma once` skips the re-include.
// Mirror the define so the first parse rewrites the class to all-public for the whole TU.
#if PLATFORM_WINDOWS
// An upstream include leaks the Win32 Interlocked* macros, which mangle
// FPlatformAtomics::InterlockedIncrement -> ::_InterlockedIncrement inside RenderCore headers
// (RenderTargetPool.h) that SceneRendering.h transitively pulls in. Allow+Hide is a no-op pair
// that re-asserts and then clears those macros, so SceneRendering.h parses against the real
// FPlatformAtomics API.
#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformAtomics.h"
#endif
#define private public
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "SceneCore.h"
#include "SceneViewState.h"
#undef private

namespace TempoMultiViewCapture
{
namespace
{
	// Mirrors (and multi-view-generalizes) SetupViewFamilyForSceneCapture. Owner-level fields come
	// from PrimaryComponent; per-view fields come from FViewSetup.
	TArray<FSceneView*> SetupMultiViewFamily(
		FSceneViewFamily& ViewFamily,
		USceneCaptureComponent2D* PrimaryComponent,
		bool bCaptureSceneColor,
		TArrayView<const FViewSetup> Views)
	{
		check(!ViewFamily.GetScreenPercentageInterface());
		check(PrimaryComponent);

		ViewFamily.FrameNumber = ViewFamily.Scene->GetFrameNumber();
		ViewFamily.FrameCounter = GFrameCounter;

		// Owner-level hide/show lists and first-person parameters, computed once per owner.
		struct FOwnerSettings
		{
			TSet<FPrimitiveComponentId> HiddenPrimitives;
			TOptional<TSet<FPrimitiveComponentId>> ShowOnlyPrimitives;
			FFirstPersonParameters FirstPersonParams;
		};
		TMap<const USceneCaptureComponent2D*, FOwnerSettings> OwnerSettingsMap;
		auto GetOwnerSettings = [&OwnerSettingsMap](USceneCaptureComponent2D* Owner) -> const FOwnerSettings&
		{
			if (const FOwnerSettings* Found = OwnerSettingsMap.Find(Owner))
			{
				return *Found;
			}
			FOwnerSettings& Settings = OwnerSettingsMap.Add(Owner);
			for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : Owner->HiddenComponents)
			{
				if (UPrimitiveComponent* Prim = WeakPrim.Get())
				{
					Settings.HiddenPrimitives.Add(Prim->GetPrimitiveSceneId());
				}
			}
			for (AActor* Actor : Owner->HiddenActors)
			{
				if (Actor)
				{
					for (UActorComponent* ActorComp : Actor->GetComponents())
					{
						if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp))
						{
							Settings.HiddenPrimitives.Add(PrimComp->GetPrimitiveSceneId());
						}
					}
				}
			}

			if (Owner->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
			{
				Settings.ShowOnlyPrimitives.Emplace();
				for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : Owner->ShowOnlyComponents)
				{
					if (UPrimitiveComponent* Prim = WeakPrim.Get())
					{
						Settings.ShowOnlyPrimitives->Add(Prim->GetPrimitiveSceneId());
					}
				}
				for (AActor* Actor : Owner->ShowOnlyActors)
				{
					if (Actor)
					{
						for (UActorComponent* ActorComp : Actor->GetComponents())
						{
							if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp))
							{
								Settings.ShowOnlyPrimitives->Add(PrimComp->GetPrimitiveSceneId());
							}
						}
					}
				}
			}

			FMinimalViewInfo ViewInfo;
			Owner->GetCameraView(0.0f, ViewInfo);
			Settings.FirstPersonParams = FFirstPersonParameters(
				ViewInfo.CalculateFirstPersonFOVCorrectionFactor(),
				ViewInfo.FirstPersonScale,
				ViewInfo.bUseFirstPersonParameters);
			return Settings;
		};

		TArray<FSceneView*> ViewPtrArray;
		ViewPtrArray.Reserve(Views.Num());

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewSetup& Setup = Views[ViewIndex];
			check(Setup.PostProcessSettings);
			USceneCaptureComponent2D* Owner = Setup.OwnerComponent ? Setup.OwnerComponent : PrimaryComponent;
			const FOwnerSettings& OwnerSettings = GetOwnerSettings(Owner);

			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.SetViewRectangle(Setup.ViewRect);
			ViewInitOptions.ViewFamily = &ViewFamily;
			ViewInitOptions.ViewActor = Owner->GetViewOwner();
			ViewInitOptions.ViewLocation = Setup.ViewLocation;
			ViewInitOptions.ViewOrigin = Setup.ViewLocation;
			ViewInitOptions.ViewRotationMatrix = Setup.ViewRotationMatrix;
			ViewInitOptions.BackgroundColor = FLinearColor::Black;
			ViewInitOptions.OverrideFarClippingPlaneDistance = Owner->MaxViewDistanceOverride;
			ViewInitOptions.StereoPass = EStereoscopicPass::eSSP_FULL;
			ViewInitOptions.StereoViewIndex = INDEX_NONE;
			ViewInitOptions.ProjectionMatrix = Setup.ProjectionMatrix;
			ViewInitOptions.bIsSceneCapture = true;
			ViewInitOptions.bIsPlanarReflection = false;
			ViewInitOptions.FOV = Setup.FOV;
			ViewInitOptions.DesiredFOV = Setup.FOV;

			if (ViewFamily.Scene->GetWorld() != nullptr && ViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
			{
				ViewInitOptions.WorldToMetersScale = ViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
			}

			if (bCaptureSceneColor)
			{
				ViewFamily.EngineShowFlags.PostProcessing = 0;
				ViewInitOptions.OverlayColor = FLinearColor::Black;
			}

			// Per-tile view state: critical for TAA history independence per tile.
			ViewInitOptions.SceneViewStateInterface = Setup.ViewState;
			// By default one eye adaptation state for the whole family, the engine's own facility for
			// tiled rendering (Movie Render Queue's high-resolution tiles use it). Every tile's exposure
			// buffer and PreExposure then come from the same view state, so tiles cannot drift apart
			// even through a readback race. Tiles run manual exposure, so no per-tile histogram is
			// lost by sharing. Views of different cameras name their own camera's state instead.
			ViewInitOptions.ExposureSceneViewStateInterface = Setup.ExposureViewState ? Setup.ExposureViewState : Views[0].ViewState;
			ViewInitOptions.LODDistanceFactor = FMath::Clamp(Owner->LODDistanceFactor, .01f, 100.0f);
			// Hack to pass Lumen's LUMEN_MAX_VIEWS=2 family-level gate (Lumen.cpp:239). The cube
			// path in LumenSceneRendering.cpp:3099 is semantically honest for us: all tiles share
			// the rig origin, and Lumen uses a single omnidirectional origin from Views[0] — which
			// is exactly what a multi-tile camera needs. Applied to every view so DFAO temporal
			// history is suppressed uniformly (DistanceFieldLightingPost.cpp:305) — otherwise
			// Views[0] would look noisier than the rest and leave a seam at the tile boundary.
			// Eye adaptation sharing, which the cube path also implies, is set explicitly above for
			// every tile count. Guarded on Views.Num() > 2 so we only take the cube-path hit when
			// we'd otherwise lose Lumen entirely.
			ViewInitOptions.bIsSceneCaptureCube = Views.Num() > 2;
			// Engine's SetupViewFamilyForSceneCapture consults GRayTracingSceneCaptures (a Renderer-
			// private CVar used only for debug overrides). We don't link Renderer (see Build.cs note),
			// so just honor the primary's flag.
			ViewInitOptions.bSceneCaptureUsesRayTracing = Owner->bUseRayTracingIfEnabled;
			ViewInitOptions.bExcludeFromSceneTextureExtents = Owner->bExcludeFromSceneTextureExtents;
			ViewInitOptions.FirstPersonParams = OwnerSettings.FirstPersonParams;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
			// New in UE 5.8: per-view skylight scale, sourced from the capture component (the engine's
			// SetupViewFamilyForSceneCapture does the same). Defaults to white when unset.
			ViewInitOptions.SkylightScale = Owner->SkylightScale;
#endif

			FSceneView* View = new FSceneView(ViewInitOptions);
			View->HiddenPrimitives = OwnerSettings.HiddenPrimitives;
			View->ShowOnlyPrimitives = OwnerSettings.ShowOnlyPrimitives;

			ViewFamily.Views.Add(View);
			ViewPtrArray.Add(View);

			View->StartFinalPostprocessSettings(Setup.ViewLocation);

			// Scene-capture default: Lumen disabled unless the tile's PPM re-enables it.
			View->FinalPostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
			View->FinalPostProcessSettings.ReflectionMethod = EReflectionMethod::None;
			View->FinalPostProcessSettings.LumenSurfaceCacheResolution = 0.5f;

			// Apply PP settings, but strip UMaterialInterface blendables (post-process materials) and
			// push them ourselves below. The engine's UMaterialInterface::OverrideBlendableSettings
			// allocates a "reusable MID" via FSceneViewState::GetReusableMID and stashes it in the
			// view state's MIDPool. In this multi-view path that pool entry is not reliably kept
			// alive across GC — `obj gc` between captures collects it, and the next frame derefs
			// freed memory inside ClearParameterValuesInternal. Pushing FPostProcessMaterialNode
			// directly with our own UPROPERTY-tracked MID skips that pool entirely.
			FPostProcessSettings PPSettingsStripped = *Setup.PostProcessSettings;
			TArray<FWeightedBlendable, TInlineAllocator<2>> PPMBlendables;
			PPSettingsStripped.WeightedBlendables.Array.RemoveAll(
				[&PPMBlendables](const FWeightedBlendable& WB)
				{
					if (Cast<UMaterialInterface>(WB.Object))
					{
						PPMBlendables.Add(WB);
						return true;
					}
					return false;
				});

			View->OverridePostProcessSettings(PPSettingsStripped, Setup.PostProcessBlendWeight);

			for (const FWeightedBlendable& WB : PPMBlendables)
			{
				UMaterialInterface* MI = Cast<UMaterialInterface>(WB.Object);
				const UMaterial* Base = MI->GetMaterial();
				if (!Base || Base->MaterialDomain != MD_PostProcess)
				{
					continue;
				}
				const float EffectiveWeight = WB.Weight * Setup.PostProcessBlendWeight;
				if (!(EffectiveWeight > 0.0f && EffectiveWeight <= 1.0f))
				{
					continue;
				}
				const bool bIsBlendable = Base->bIsBlendable && MI->GetUserSceneTextureOutput(Base) == NAME_None;
				FPostProcessMaterialNode Node(
					MI,
					MI->GetBlendableLocation(Base),
					MI->GetBlendablePriority(Base),
					bIsBlendable);
				View->FinalPostProcessSettings.BlendableManager.PushBlendableData(EffectiveWeight, Node);
			}

			View->EndFinalPostprocessSettings(ViewInitOptions);

			View->ViewLightingChannelMask = Owner->ViewLightingChannels.GetMaskForStruct();

			View->bCameraCut = Setup.bCameraCut;

			if (Owner->bEnableClipPlane)
			{
				View->GlobalClippingPlane = FPlane(
					Owner->ClipPlaneBase,
					Owner->ClipPlaneNormal.GetSafeNormal());
				View->bAllowTemporalJitter = false;
			}
		}

		return ViewPtrArray;
	}

	// Mirrors SetupSceneViewExtensionsForSceneCapture (806-822). Small enough to reproduce.
	void SetupViewExtensions(FSceneViewFamily& ViewFamily, TConstArrayView<FSceneView*> Views)
	{
		for (const FSceneViewExtensionRef& Extension : ViewFamily.ViewExtensions)
		{
			Extension->SetupViewFamily(ViewFamily);
		}

		for (FSceneView* View : Views)
		{
			for (const FSceneViewExtensionRef& Extension : ViewFamily.ViewExtensions)
			{
				Extension->SetupView(ViewFamily, *View);
			}
		}
	}
} // namespace

void RenderTiles(
	FSceneInterface* Scene,
	USceneCaptureComponent2D* PrimaryComponent,
	UTextureRenderTarget2D* AtlasRT,
	TArrayView<const FViewSetup> Views,
	ESceneCaptureSource CaptureSource,
	float ResolutionFraction)
{
	check(IsInGameThread());
	check(Scene);

	TUniquePtr<ISceneRenderBuilder> Builder = ISceneRenderBuilder::Create(Scene);
	if (!Builder.IsValid())
	{
		return;
	}
	if (AddTilesRenderer(*Builder, Scene, PrimaryComponent, AtlasRT, Views, CaptureSource, ResolutionFraction))
	{
		Builder->Execute();
	}
}

bool AddTilesRenderer(
	ISceneRenderBuilder& Builder,
	FSceneInterface* Scene,
	USceneCaptureComponent2D* PrimaryComponent,
	UTextureRenderTarget2D* AtlasRT,
	TArrayView<const FViewSetup> Views,
	ESceneCaptureSource CaptureSource,
	float ResolutionFraction,
	TArrayView<const FAtlasRectCopy> Copies)
{
	check(IsInGameThread());
	check(Scene && PrimaryComponent && AtlasRT);
	check(!Views.IsEmpty());

	FTextureRenderTargetResource* AtlasResource = AtlasRT->GameThread_GetRenderTargetResource();
	if (!AtlasResource)
	{
		return false;
	}

	struct FCopyTarget
	{
		FIntRect SourceRect;
		FTextureRenderTargetResource* Resource = nullptr;
	};
	TArray<FCopyTarget> CopyTargets;
	for (const FAtlasRectCopy& Copy : Copies)
	{
		FTextureRenderTargetResource* Resource = Copy.Destination ? Copy.Destination->GameThread_GetRenderTargetResource() : nullptr;
		if (Resource && Copy.SourceRect.Area() > 0)
		{
			CopyTargets.Add({ Copy.SourceRect, Resource });
		}
	}

	const bool bCaptureSceneColor =
		CaptureSource == SCS_SceneColorHDR
		|| CaptureSource == SCS_SceneColorHDRNoAlpha
		|| CaptureSource == SCS_SceneColorSceneDepth
		|| CaptureSource == SCS_SceneDepth
		|| CaptureSource == SCS_DeviceDepth
		|| CaptureSource == SCS_Normal
		|| CaptureSource == SCS_BaseColor;

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		AtlasResource, Scene, PrimaryComponent->ShowFlags)
		.SetResolveScene(!bCaptureSceneColor)
		.SetRealtimeUpdate(PrimaryComponent->bCaptureEveryFrame || PrimaryComponent->bAlwaysPersistRenderingState));

	FSceneViewExtensionContext ViewExtensionContext(Scene);
	ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(ViewExtensionContext);

	// Component-local view extensions on the primary.
	for (int32 Index = 0; Index < PrimaryComponent->SceneViewExtensions.Num(); )
	{
		TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> Extension = PrimaryComponent->SceneViewExtensions[Index].Pin();
		if (Extension.IsValid())
		{
			if (Extension->IsActiveThisFrame(ViewExtensionContext))
			{
				ViewFamily.ViewExtensions.Add(Extension.ToSharedRef());
			}
			++Index;
		}
		else
		{
			PrimaryComponent->SceneViewExtensions.RemoveAt(Index, EAllowShrinking::No);
		}
	}

	TArray<FSceneView*> FamilyViews = SetupMultiViewFamily(ViewFamily, PrimaryComponent, bCaptureSceneColor, Views);

	ViewFamily.SceneCaptureSource = CaptureSource;
	ViewFamily.SceneCaptureCompositeMode = PrimaryComponent->CompositeMode;

	// Engine's 2D scene-capture path hardcodes GlobalResolutionFraction=1.0 with the comment
	// "Screen percentage is still not supported in scene capture" (SceneCaptureRendering.cpp:911-915),
	// but that's a UX limitation (no slider on USceneCaptureComponent2D) rather than a renderer
	// constraint — the same engine path forwards a non-1.0 fraction via Fork_GameThread when a
	// scene capture has bRenderWithMainViewResolution set. We accept a caller-supplied fraction
	// and feed it directly to FLegacyScreenPercentageDriver. ScreenPercentage show-flag stays
	// off because we drive the fraction via the driver, not via the show-flag CVar fallback.
	ViewFamily.EngineShowFlags.ScreenPercentage = !FMath::IsNearlyEqual(ResolutionFraction, 1.0f, UE_KINDA_SMALL_NUMBER);
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, ResolutionFraction));

	if (PrimaryComponent->IsUnlit())
	{
		const bool bAllowAtmosphere =
			CaptureSource == SCS_SceneColorHDR
			|| CaptureSource == SCS_SceneColorHDRNoAlpha
			|| CaptureSource == SCS_SceneColorSceneDepth;
		ViewFamily.EngineShowFlags.DisableFeaturesForUnlit(bAllowAtmosphere);
	}

	SetupViewExtensions(ViewFamily, FamilyViews);

	FSceneRenderer* SceneRenderer = Builder.CreateSceneRenderer(&ViewFamily);
	check(SceneRenderer);

	// Per-view Lumen scene data hookup. Engine does this against a single view state; we repeat
	// per tile so each tile's view state owns a Lumen scene entry when its own PPM requests Lumen.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FSceneViewStateInterface* ViewStateInterface = Views[ViewIndex].ViewState;
		if (!ViewStateInterface)
		{
			continue;
		}

		const FFinalPostProcessSettings& FinalPP = SceneRenderer->Views[ViewIndex].FinalPostProcessSettings;
		const bool bUsesLumen =
			FinalPP.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::Lumen
			|| FinalPP.ReflectionMethod == EReflectionMethod::Lumen;

		if (bUsesLumen)
		{
			ViewStateInterface->AddLumenSceneData(Scene, FinalPP.LumenSurfaceCacheResolution);
		}
		else
		{
			ViewStateInterface->RemoveLumenSceneData(Scene);
		}
	}

	// The atlas exists outside the view rects if tiles don't fully cover it. Clear on render-thread
	// before renderer writes each view's ViewRect. Safer than relying on tile rects tiling exactly.
	const FIntPoint AtlasSize(AtlasRT->SizeX, AtlasRT->SizeY);

	Builder.AddRenderer(
		SceneRenderer,
		TEXT("TempoTilesMultiView"),
		[AtlasResource, AtlasSize, CopyTargets = MoveTemp(CopyTargets)](FRDGBuilder& GraphBuilder, const FSceneRenderFunctionInputs& Inputs) -> bool
		{
			FRDGTextureRef AtlasTexture = RegisterExternalTexture(
				GraphBuilder,
				AtlasResource->GetRenderTargetTexture(),
				TEXT("TempoMultiViewAtlas"));

			// Clear the whole atlas before the views render into their respective rects. Engine's
			// 2D capture path clears only Views[0].UnscaledViewRect on the assumption of one view;
			// for multi-view we clear the full atlas.
			AddClearRenderTargetPass(
				GraphBuilder,
				AtlasTexture,
				FLinearColor::Black,
				FIntRect(0, 0, AtlasSize.X, AtlasSize.Y));

			Inputs.Renderer->Render(GraphBuilder, Inputs.SceneUpdateInputs);

			// Same format on both sides, so each copy moves the rendered bytes unchanged.
			for (const FCopyTarget& Copy : CopyTargets)
			{
				FRDGTextureRef Destination = RegisterExternalTexture(
					GraphBuilder,
					Copy.Resource->GetRenderTargetTexture(),
					TEXT("TempoMultiViewCopyDestination"));
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.SourcePosition = FIntVector(Copy.SourceRect.Min.X, Copy.SourceRect.Min.Y, 0);
				CopyInfo.Size = FIntVector(Copy.SourceRect.Width(), Copy.SourceRect.Height(), 1);
				AddCopyTexturePass(GraphBuilder, AtlasTexture, Destination, CopyInfo);
				GraphBuilder.SetTextureAccessFinal(Destination, ERHIAccess::SRVMask);
			}

			GraphBuilder.SetTextureAccessFinal(AtlasTexture, ERHIAccess::SRVMask);
			return true;
		});

	return true;
}

void FCaptureSink::Command(TUniqueFunction<void()>&& Function)
{
	if (Builder)
	{
		Builder->AddCommand(MoveTemp(Function));
	}
	else
	{
		Function();
	}
}

void FCaptureSink::RenderTiles(
	FSceneInterface* Scene,
	USceneCaptureComponent2D* PrimaryComponent,
	UTextureRenderTarget2D* AtlasRT,
	TArrayView<const FViewSetup> Views,
	ESceneCaptureSource CaptureSource,
	float ResolutionFraction,
	TArrayView<const FAtlasRectCopy> Copies)
{
	if (Builder)
	{
		AddTilesRenderer(*Builder, Scene, PrimaryComponent, AtlasRT, Views, CaptureSource, ResolutionFraction, Copies);
		return;
	}
	TUniquePtr<ISceneRenderBuilder> OwnBuilder = ISceneRenderBuilder::Create(Scene);
	if (OwnBuilder.IsValid() && AddTilesRenderer(*OwnBuilder, Scene, PrimaryComponent, AtlasRT, Views, CaptureSource, ResolutionFraction, Copies))
	{
		OwnBuilder->Execute();
	}
}

bool GetViewParticipatingMediaInputs(const FSceneView& View, FTempoLidarMediaPassInputs& OutInputs)
{
	check(IsInRenderingThread());

	// See GetRenderedViewSceneTextures for why both downcasts hold.
	const FViewFamilyInfo* ViewFamily = static_cast<const FViewFamilyInfo*>(View.Family);
	const FSceneTextures* SceneTextures = ViewFamily ? ViewFamily->GetSceneTexturesChecked() : nullptr;
	if (!SceneTextures || !SceneTextures->Depth.Resolve)
	{
		return false;
	}
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);

	OutInputs.FeatureLevel = View.GetFeatureLevel();
	OutInputs.ViewUniformBuffer = View.ViewUniformBuffer;
	OutInputs.ViewRect = ViewInfo.ViewRect;
	OutInputs.OutputRect = ViewInfo.UnscaledViewRect;
	OutInputs.SceneDepth = SceneTextures->Depth.Resolve;

	// Mirrors SetupFogUniformParameters (FogRendering.cpp): the height fog constants the fog pass
	// binds are the view's own, computed by FSceneRenderer::InitFogConstants. When fog is not
	// rendered for this family they are left at their no-fog defaults, which the shader treats as
	// full transmittance, so they are safe to pass either way.
	FTempoLidarMediaFogInputs& Fog = OutInputs.Fog;
	Fog.ExponentialFogParameters = ViewInfo.ExponentialFogParameters;
	Fog.ExponentialFogParameters2 = ViewInfo.ExponentialFogParameters2;
	Fog.ExponentialFogParameters3 = ViewInfo.ExponentialFogParameters3;
	Fog.MinFogTransmittance = 1.0f - ViewInfo.FogMaxOpacity;
	Fog.EndDistance = ViewInfo.FogEndDistance;
	Fog.VolumetricFogStartDistance = ViewInfo.VolumetricFogStartDistance;
	// Set by ComputeVolumetricFog only when the grid was rendered for this view.
	Fog.IntegratedLightScattering = ViewInfo.VolumetricFogResources.IntegratedLightScatteringTexture;

	// The fog's albedo is authored on the fog component and applies to every fog source: the height
	// fog, the grid (whose color channel is lit in-scatter, not albedo) and local fog volumes, whose
	// own albedos are not resolved individually. The view's copy is only filled in when the height
	// fog is set to match volumetric fog, so read the component's.
	Fog.Albedo = 1.0f;
	if (const FScene* RenderScene = View.Family && View.Family->Scene ? View.Family->Scene->GetRenderScene() : nullptr)
	{
		if (RenderScene->ExponentialFogs.Num() > 0)
		{
			Fog.HeightFogId = RenderScene->ExponentialFogs[0].Id;
			Fog.Albedo = RenderScene->ExponentialFogs[0].VolumetricFogAlbedo.GetLuminance();
		}
	}

	// Local fog volumes are composed analytically (in the height fog pass or their own pass) unless
	// they were injected into the volumetric fog grid, in which case the grid already carries them.
	// The view data is only built when local fog volumes render for the view; its instance count
	// and buffers say whether it was.
	const FLocalFogVolumeViewData& LFVData = ViewInfo.LocalFogVolumeViewData;
	const FLocalFogVolumeUniformParameters& LFVParameters = LFVData.UniformParametersStruct;
	const bool bLocalFogVolumesAnalytic = LFVData.GPUInstanceCount > 0
		&& LFVParameters.LocalFogVolumeCommon.LocalFogVolumeInstances != nullptr
		&& LFVParameters.LocalFogVolumeTileDataTexture != nullptr
		&& !(LFVParameters.LocalFogVolumeCommon.ShouldRenderLocalFogVolumeInVolumetricFog != 0 && Fog.IntegratedLightScattering != nullptr);
	Fog.LocalFogVolumes = bLocalFogVolumesAnalytic ? &LFVParameters : nullptr;

	return true;
}

bool GetViewTranslucentBatches(const FSceneView& View, TArray<FTempoLidarMediaTranslucentBatch>& OutBatches, const FScene*& OutScene, FSceneUniformBuffer*& OutSceneUniforms)
{
	check(IsInRenderingThread());

	const FScene* Scene = View.Family && View.Family->Scene ? View.Family->Scene->GetRenderScene() : nullptr;
	if (!Scene)
	{
		return false;
	}
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	OutScene = Scene;
	OutSceneUniforms = &ViewInfo.GetSceneUniforms();

	// Dynamic batches: everything the primitives' GetDynamicMeshElements added for this view. The
	// relevance flags cached with each say whether its material is opaque or masked, which is
	// enough to skip the bulk of them here.
	for (const FMeshBatchAndRelevance& MeshBatchAndRelevance : ViewInfo.DynamicMeshElements)
	{
		if (MeshBatchAndRelevance.GetHasOpaqueOrMaskedMaterial() || !MeshBatchAndRelevance.GetRenderInMainPass())
		{
			continue;
		}
		FTempoLidarMediaTranslucentBatch& Batch = OutBatches.AddDefaulted_GetRef();
		Batch.Mesh = MeshBatchAndRelevance.Mesh;
		Batch.Proxy = MeshBatchAndRelevance.PrimitiveSceneProxy;
		Batch.BatchElementMask = ~0ull;
		Batch.StaticMeshId = -1;
	}

	// Static batches: the visible primitives' static meshes at the LOD the view selected. The
	// per-element visibility some meshes carry (landscape sections) is not tracked; every element
	// is drawn.
	for (int32 PrimitiveIndex = 0; PrimitiveIndex < Scene->Primitives.Num(); ++PrimitiveIndex)
	{
		if (!ViewInfo.PrimitiveVisibilityMap[PrimitiveIndex])
		{
			continue;
		}
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];
		if (!PrimitiveSceneInfo || !PrimitiveSceneInfo->Proxy)
		{
			continue;
		}
		for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshes.Num(); ++MeshIndex)
		{
			const FStaticMeshBatchRelevance& Relevance = PrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
			if (!Relevance.bUseForMaterial || !ViewInfo.StaticMeshVisibilityMap[Relevance.Id])
			{
				continue;
			}
			const FStaticMeshBatch& StaticMesh = PrimitiveSceneInfo->StaticMeshes[MeshIndex];
			FTempoLidarMediaTranslucentBatch& Batch = OutBatches.AddDefaulted_GetRef();
			Batch.Mesh = &StaticMesh;
			Batch.Proxy = PrimitiveSceneInfo->Proxy;
			Batch.BatchElementMask = ~0ull;
			Batch.StaticMeshId = StaticMesh.Id;
		}
	}

	return true;
}

FScopedVolumetricFogHistoryRescale::FScopedVolumetricFogHistoryRescale(float TicksSinceLastRender)
{
	check(IsInGameThread());
	if (TicksSinceLastRender <= 1.0f)
	{
		return;
	}
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricFog.HistoryWeight"));
	if (!Variable)
	{
		return;
	}
	const float Weight = Variable->GetFloat();
	// Zero keeps no history and one never converges; neither has a meaningful power.
	if (Weight <= 0.0f || Weight >= 1.0f)
	{
		return;
	}
	HistoryWeight = Variable;
	OriginalWeight = Weight;
	HistoryWeight->SetWithCurrentPriority(FMath::Pow(Weight, TicksSinceLastRender), NAME_None, ECVF_SetByConsole, ECVF_SetByScalability);
}

FScopedVolumetricFogHistoryRescale::FScopedVolumetricFogHistoryRescale(float TicksSinceLastRender, ISceneRenderBuilder* InBuilder)
	: FScopedVolumetricFogHistoryRescale(InBuilder ? 1.0f : TicksSinceLastRender)
{
	check(IsInGameThread());
	if (!InBuilder || TicksSinceLastRender <= 1.0f)
	{
		return;
	}
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricFog.HistoryWeight"));
	if (!Variable)
	{
		return;
	}
	// The builder runs its commands on the game thread in order with its renders, so the weight is
	// set right before the renders added inside the scope are enqueued and restored right after.
	Builder = InBuilder;
	BuilderOriginalWeight = MakeShared<float>(-1.0f);
	Builder->AddCommand([Variable, TicksSinceLastRender, Saved = BuilderOriginalWeight]()
	{
		const float Weight = Variable->GetFloat();
		if (Weight <= 0.0f || Weight >= 1.0f)
		{
			return;
		}
		*Saved = Weight;
		Variable->SetWithCurrentPriority(FMath::Pow(Weight, TicksSinceLastRender), NAME_None, ECVF_SetByConsole, ECVF_SetByScalability);
	});
	HistoryWeight = Variable;
}

FScopedVolumetricFogHistoryRescale::~FScopedVolumetricFogHistoryRescale()
{
	if (Builder)
	{
		Builder->AddCommand([Variable = HistoryWeight, Saved = BuilderOriginalWeight]()
		{
			if (*Saved >= 0.0f)
			{
				Variable->SetWithCurrentPriority(*Saved, NAME_None, ECVF_SetByConsole, ECVF_SetByScalability);
			}
		});
		return;
	}
	if (HistoryWeight)
	{
		HistoryWeight->SetWithCurrentPriority(OriginalWeight, NAME_None, ECVF_SetByConsole, ECVF_SetByScalability);
	}
}

bool GetRenderedViewSceneTextures(const FSceneView& View, FRDGTextureRef& OutVelocity, FRDGTextureRef& OutSceneDepth, FIntRect& OutViewRect)
{
	check(IsInRenderingThread());

	// The renderer re-points every view's Family at its own FViewFamilyInfo and renders FViewInfos
	// (FSceneRenderer constructor), so both downcasts hold for any view a render-thread hook sees.
	const FViewFamilyInfo* ViewFamily = static_cast<const FViewFamilyInfo*>(View.Family);
	const FSceneTextures* SceneTextures = ViewFamily ? ViewFamily->GetSceneTexturesChecked() : nullptr;
	if (!SceneTextures)
	{
		return false;
	}

	OutVelocity = SceneTextures->Velocity;
	OutSceneDepth = SceneTextures->Depth.Resolve;
	OutViewRect = static_cast<const FViewInfo&>(View).ViewRect;
	return OutVelocity != nullptr && OutSceneDepth != nullptr;
}

void SetLumenSceneOwner(FSceneViewStateInterface* ViewState, FSceneViewStateInterface* Owner)
{
	check(IsInGameThread());
	if (!ViewState)
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(TempoSetLumenSceneOwner)([ViewState, Owner](FRHICommandListImmediate&)
	{
		// Lumen finds a view's scene data by the share-origin key (FScene::FindLumenSceneData); it is the
		// view state's own key unless the state was allocated to share another's origin.
		FSceneViewState* State = static_cast<FSceneViewState*>(ViewState);
		State->ShareOriginUniqueID = Owner ? static_cast<FSceneViewState*>(Owner)->UniqueID : State->UniqueID;
	});
}

void SkipCustomDepthPass(FSceneView& View)
{
	check(IsInRenderingThread());

	// Visibility sets this from the relevance of the primitives the view saw (SceneVisibility.cpp);
	// the custom depth pass renders only the views that have it (CustomDepthRendering.cpp). See
	// GetRenderedViewSceneTextures for why the downcast holds.
	static_cast<FViewInfo&>(View).bHasCustomDepthPrimitives = false;
}

} // namespace TempoMultiViewCapture
