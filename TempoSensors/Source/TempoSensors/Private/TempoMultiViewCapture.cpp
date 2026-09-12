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
#undef private

namespace TempoMultiViewCapture
{
namespace
{
	// Per-component view context: the owner-level fields the engine's SetupViewFamilyForSceneCapture
	// derives from the one capture component it renders. With views from several sensors in one
	// family these are per view, so they are computed once per distinct component and applied to
	// each of that component's views.
	struct FComponentViewContext
	{
		TSet<FPrimitiveComponentId> HiddenPrimitives;
		TOptional<TSet<FPrimitiveComponentId>> ShowOnlyPrimitives;
		FFirstPersonParameters FirstPersonParams;
	};

	void BuildComponentViewContext(USceneCaptureComponent2D* Component, FComponentViewContext& Out)
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : Component->HiddenComponents)
		{
			if (UPrimitiveComponent* Prim = WeakPrim.Get())
			{
				Out.HiddenPrimitives.Add(Prim->GetPrimitiveSceneId());
			}
		}
		for (AActor* Actor : Component->HiddenActors)
		{
			if (Actor)
			{
				for (UActorComponent* ActorComp : Actor->GetComponents())
				{
					if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp))
					{
						Out.HiddenPrimitives.Add(PrimComp->GetPrimitiveSceneId());
					}
				}
			}
		}

		if (Component->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
		{
			Out.ShowOnlyPrimitives.Emplace();
			for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : Component->ShowOnlyComponents)
			{
				if (UPrimitiveComponent* Prim = WeakPrim.Get())
				{
					Out.ShowOnlyPrimitives->Add(Prim->GetPrimitiveSceneId());
				}
			}
			for (AActor* Actor : Component->ShowOnlyActors)
			{
				if (Actor)
				{
					for (UActorComponent* ActorComp : Actor->GetComponents())
					{
						if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp))
						{
							Out.ShowOnlyPrimitives->Add(PrimComp->GetPrimitiveSceneId());
						}
					}
				}
			}
		}

		FMinimalViewInfo ViewInfo;
		Component->GetCameraView(0.0f, ViewInfo);
		Out.FirstPersonParams = FFirstPersonParameters(
			ViewInfo.CalculateFirstPersonFOVCorrectionFactor(),
			ViewInfo.FirstPersonScale,
			ViewInfo.bUseFirstPersonParameters);
	}

	// Mirrors (and multi-view-generalizes) SetupViewFamilyForSceneCapture. Family-level fields come
	// from the family show flags; per-view fields come from FViewSetup and its Component.
	TArray<FSceneView*> SetupMultiViewFamily(
		FSceneViewFamily& ViewFamily,
		bool bCaptureSceneColor,
		TArrayView<const FViewSetup> Views)
	{
		check(!ViewFamily.GetScreenPercentageInterface());

		ViewFamily.FrameNumber = ViewFamily.Scene->GetFrameNumber();
		ViewFamily.FrameCounter = GFrameCounter;

		TMap<USceneCaptureComponent2D*, FComponentViewContext> ComponentContexts;
		for (const FViewSetup& Setup : Views)
		{
			check(Setup.Component);
			if (!ComponentContexts.Contains(Setup.Component))
			{
				BuildComponentViewContext(Setup.Component, ComponentContexts.Add(Setup.Component));
			}
		}

		TArray<FSceneView*> ViewPtrArray;
		ViewPtrArray.Reserve(Views.Num());

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewSetup& Setup = Views[ViewIndex];
			check(Setup.PostProcessSettings);
			USceneCaptureComponent2D* Component = Setup.Component;
			const FComponentViewContext& Context = ComponentContexts.FindChecked(Component);

			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.SetViewRectangle(Setup.ViewRect);
			ViewInitOptions.ViewFamily = &ViewFamily;
			ViewInitOptions.ViewActor = Component->GetViewOwner();
			ViewInitOptions.ViewLocation = Setup.ViewLocation;
			ViewInitOptions.ViewOrigin = Setup.ViewLocation;
			ViewInitOptions.ViewRotationMatrix = Setup.ViewRotationMatrix;
			ViewInitOptions.BackgroundColor = FLinearColor::Black;
			ViewInitOptions.OverrideFarClippingPlaneDistance = Component->MaxViewDistanceOverride;
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
			// Eye adaptation state is the caller's choice per view. The tiles of one sensor share
			// that sensor's first tile — the engine's own facility for tiled rendering (Movie Render
			// Queue's high-resolution tiles use it) — so every tile's exposure buffer and PreExposure
			// come from the same view state and tiles cannot drift apart even through a readback
			// race. Views from different sensors keep their own so each meters its own scene. Null
			// leaves the engine default, which is the view's own state.
			ViewInitOptions.ExposureSceneViewStateInterface = Setup.ExposureViewState;
			ViewInitOptions.LODDistanceFactor = FMath::Clamp(Component->LODDistanceFactor, .01f, 100.0f);
			// Hack to pass Lumen's LUMEN_MAX_VIEWS=2 family-level gate (Lumen.cpp:239). The cube
			// path in LumenSceneRendering.cpp:3099 collapses Lumen's view origins to Views[0] with
			// an accept-everything frustum. Only the surface cache update prioritizes by that
			// origin; the screen probe gather, radiance cache and global distance field stay per
			// view. So this is exact for the tiles of one sensor and a few meters off for other
			// sensors on the same rig, which is within the card radius. Applied to every view so
			// DFAO temporal history is suppressed uniformly (DistanceFieldLightingPost.cpp:305) —
			// otherwise Views[0] would look noisier than the rest and leave a seam at the tile
			// boundary. Guarded on Views.Num() > 2 so we only take the cube-path hit when we'd
			// otherwise lose Lumen entirely.
			ViewInitOptions.bIsSceneCaptureCube = Views.Num() > 2;
			// Engine's SetupViewFamilyForSceneCapture consults GRayTracingSceneCaptures (a Renderer-
			// private CVar used only for debug overrides). We don't link Renderer (see Build.cs note),
			// so just honor the component's flag.
			ViewInitOptions.bSceneCaptureUsesRayTracing = Component->bUseRayTracingIfEnabled;
			ViewInitOptions.bExcludeFromSceneTextureExtents = Component->bExcludeFromSceneTextureExtents;
			ViewInitOptions.FirstPersonParams = Context.FirstPersonParams;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
			// New in UE 5.8: per-view skylight scale, sourced from the capture component (the engine's
			// SetupViewFamilyForSceneCapture does the same). Defaults to white when unset.
			ViewInitOptions.SkylightScale = Component->SkylightScale;
#endif

			FSceneView* View = new FSceneView(ViewInitOptions);
			View->HiddenPrimitives = Context.HiddenPrimitives;
			View->ShowOnlyPrimitives = Context.ShowOnlyPrimitives;

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

			View->ViewLightingChannelMask = Component->ViewLightingChannels.GetMaskForStruct();

			View->bCameraCut = Setup.bCameraCut;

			if (Component->bEnableClipPlane)
			{
				View->GlobalClippingPlane = FPlane(
					Component->ClipPlaneBase,
					Component->ClipPlaneNormal.GetSafeNormal());
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
	float ResolutionFraction,
	const FEngineShowFlags* ShowFlagsOverride)
{
	check(IsInGameThread());
	check(Scene && PrimaryComponent && AtlasRT);
	check(!Views.IsEmpty());

	FTextureRenderTargetResource* AtlasResource = AtlasRT->GameThread_GetRenderTargetResource();
	if (!AtlasResource)
	{
		return;
	}

	TUniquePtr<ISceneRenderBuilder> Builder = ISceneRenderBuilder::Create(Scene);
	if (!Builder.IsValid())
	{
		return;
	}

	const bool bCaptureSceneColor =
		CaptureSource == SCS_SceneColorHDR
		|| CaptureSource == SCS_SceneColorHDRNoAlpha
		|| CaptureSource == SCS_SceneColorSceneDepth
		|| CaptureSource == SCS_SceneDepth
		|| CaptureSource == SCS_DeviceDepth
		|| CaptureSource == SCS_Normal
		|| CaptureSource == SCS_BaseColor;

	const FEngineShowFlags& FamilyShowFlags = ShowFlagsOverride ? *ShowFlagsOverride : PrimaryComponent->ShowFlags;

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		AtlasResource, Scene, FamilyShowFlags)
		.SetResolveScene(!bCaptureSceneColor)
		.SetRealtimeUpdate(PrimaryComponent->bCaptureEveryFrame || PrimaryComponent->bAlwaysPersistRenderingState));

	FSceneViewExtensionContext ViewExtensionContext(Scene);
	ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(ViewExtensionContext);

	// Component-local view extensions, from every distinct component with a view in the family.
	TArray<USceneCaptureComponent2D*, TInlineAllocator<8>> Components;
	for (const FViewSetup& Setup : Views)
	{
		check(Setup.Component);
		Components.AddUnique(Setup.Component);
	}
	for (USceneCaptureComponent2D* Component : Components)
	{
		for (int32 Index = 0; Index < Component->SceneViewExtensions.Num(); )
		{
			TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> Extension = Component->SceneViewExtensions[Index].Pin();
			if (Extension.IsValid())
			{
				if (Extension->IsActiveThisFrame(ViewExtensionContext))
				{
					ViewFamily.ViewExtensions.AddUnique(Extension.ToSharedRef());
				}
				++Index;
			}
			else
			{
				Component->SceneViewExtensions.RemoveAt(Index, EAllowShrinking::No);
			}
		}
	}

	TArray<FSceneView*> FamilyViews = SetupMultiViewFamily(ViewFamily, bCaptureSceneColor, Views);

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

	// USceneCaptureComponent::IsUnlit, evaluated against the family's show flags rather than the
	// primary's own (which ShowFlagsOverride may have replaced).
	const bool bUnlit =
		CaptureSource == SCS_SceneDepth
		|| CaptureSource == SCS_DeviceDepth
		|| CaptureSource == SCS_Normal
		|| CaptureSource == SCS_BaseColor
		|| (!FamilyShowFlags.Lighting
			&& (CaptureSource == SCS_SceneColorHDR
				|| CaptureSource == SCS_SceneColorHDRNoAlpha
				|| CaptureSource == SCS_SceneColorSceneDepth));
	if (bUnlit)
	{
		const bool bAllowAtmosphere =
			CaptureSource == SCS_SceneColorHDR
			|| CaptureSource == SCS_SceneColorHDRNoAlpha
			|| CaptureSource == SCS_SceneColorSceneDepth;
		ViewFamily.EngineShowFlags.DisableFeaturesForUnlit(bAllowAtmosphere);
	}

	SetupViewExtensions(ViewFamily, FamilyViews);

	FSceneRenderer* SceneRenderer = Builder->CreateSceneRenderer(&ViewFamily);
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

	Builder->AddRenderer(
		SceneRenderer,
		TEXT("TempoTilesMultiView"),
		[AtlasResource, AtlasSize](FRDGBuilder& GraphBuilder, const FSceneRenderFunctionInputs& Inputs) -> bool
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

			GraphBuilder.SetTextureAccessFinal(AtlasTexture, ERHIAccess::SRVMask);
			return true;
		});

	Builder->Execute();
}

} // namespace TempoMultiViewCapture
