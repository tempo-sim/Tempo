// Copyright Tempo Simulation, LLC. All Rights Reserved

// This helper mirrors engine-private logic in Renderer/Private/SceneCaptureRendering.cpp —
// specifically SetupViewFamilyForSceneCapture (666), SetupSceneViewExtensionsForSceneCapture
// (806), CreateSceneRendererForSceneCapture (824), and UpdateSceneCaptureContent_RenderThread
// (415). Version-gated: any 5.7 engine drift will require re-pinning these snippets.
#if !(ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 7)
#error "TempoMultiViewCapture is pinned to UE 5.7 engine internals. Re-diff and update for the new engine version."
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
// SceneRendering.h transitively includes RayTracing/RayTracingScene.h, which declares private
// members that TempoSceneCaptureComponent2D.cpp relies on accessing via a `#define private public`
// hack. UBT combines both .cpp files into one Unity translation unit; this file comes first
// alphabetically, so RayTracingScene.h gets parsed here. Without mirroring the define, the class
// is parsed with real `private` — the later hack in TempoSceneCaptureComponent2D.cpp is then
// a no-op because `#pragma once` skips the re-include. Mirror the define so the first parse
// rewrites the class to all-public for the whole TU.
#define private public
#include "SceneRendering.h"
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

		// Owner-level hide/show lists, computed once.
		TSet<FPrimitiveComponentId> HiddenPrimitives;
		TOptional<TSet<FPrimitiveComponentId>> ShowOnlyPrimitives;
		{
			for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : PrimaryComponent->HiddenComponents)
			{
				if (UPrimitiveComponent* Prim = WeakPrim.Get())
				{
					HiddenPrimitives.Add(Prim->GetPrimitiveSceneId());
				}
			}
			for (AActor* Actor : PrimaryComponent->HiddenActors)
			{
				if (Actor)
				{
					for (UActorComponent* ActorComp : Actor->GetComponents())
					{
						if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp))
						{
							HiddenPrimitives.Add(PrimComp->GetPrimitiveSceneId());
						}
					}
				}
			}

			if (PrimaryComponent->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
			{
				ShowOnlyPrimitives.Emplace();
				for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : PrimaryComponent->ShowOnlyComponents)
				{
					if (UPrimitiveComponent* Prim = WeakPrim.Get())
					{
						ShowOnlyPrimitives->Add(Prim->GetPrimitiveSceneId());
					}
				}
				for (AActor* Actor : PrimaryComponent->ShowOnlyActors)
				{
					if (Actor)
					{
						for (UActorComponent* ActorComp : Actor->GetComponents())
						{
							if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp))
							{
								ShowOnlyPrimitives->Add(PrimComp->GetPrimitiveSceneId());
							}
						}
					}
				}
			}
		}

		FFirstPersonParameters FirstPersonParams;
		{
			FMinimalViewInfo ViewInfo;
			PrimaryComponent->GetCameraView(0.0f, ViewInfo);
			FirstPersonParams = FFirstPersonParameters(
				ViewInfo.CalculateFirstPersonFOVCorrectionFactor(),
				ViewInfo.FirstPersonScale,
				ViewInfo.bUseFirstPersonParameters);
		}

		TArray<FSceneView*> ViewPtrArray;
		ViewPtrArray.Reserve(Views.Num());

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewSetup& Setup = Views[ViewIndex];
			check(Setup.PostProcessSettings);

			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.SetViewRectangle(Setup.ViewRect);
			ViewInitOptions.ViewFamily = &ViewFamily;
			ViewInitOptions.ViewActor = PrimaryComponent->GetViewOwner();
			ViewInitOptions.ViewLocation = Setup.ViewLocation;
			ViewInitOptions.ViewOrigin = Setup.ViewLocation;
			ViewInitOptions.ViewRotationMatrix = Setup.ViewRotationMatrix;
			ViewInitOptions.BackgroundColor = FLinearColor::Black;
			ViewInitOptions.OverrideFarClippingPlaneDistance = PrimaryComponent->MaxViewDistanceOverride;
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

			// Per-tile view state: critical for TAA / auto-exposure history independence per tile.
			ViewInitOptions.SceneViewStateInterface = Setup.ViewState;
			ViewInitOptions.LODDistanceFactor = FMath::Clamp(PrimaryComponent->LODDistanceFactor, .01f, 100.0f);
			// Hack to pass Lumen's LUMEN_MAX_VIEWS=2 family-level gate (Lumen.cpp:239). The cube
			// path in LumenSceneRendering.cpp:3099 is semantically honest for us: all tiles share
			// the rig origin, and Lumen uses a single omnidirectional origin from Views[0] — which
			// is exactly what a multi-tile camera needs. Applied to every view so DFAO temporal
			// history is suppressed uniformly (DistanceFieldLightingPost.cpp:305) — otherwise
			// Views[0] would look noisier than the rest and leave a seam at the tile boundary.
			// Other knock-on effects (eye-adaptation sharing via SceneView.cpp:1055) are no-ops
			// here because tiles run AEM_Manual. Guarded on Views.Num() > 2 so we only take the
			// cube-path hit when we'd otherwise lose Lumen entirely.
			ViewInitOptions.bIsSceneCaptureCube = Views.Num() > 2;
			// Engine's SetupViewFamilyForSceneCapture consults GRayTracingSceneCaptures (a Renderer-
			// private CVar used only for debug overrides). We don't link Renderer (see Build.cs note),
			// so just honor the primary's flag.
			ViewInitOptions.bSceneCaptureUsesRayTracing = PrimaryComponent->bUseRayTracingIfEnabled;
			ViewInitOptions.bExcludeFromSceneTextureExtents = PrimaryComponent->bExcludeFromSceneTextureExtents;
			ViewInitOptions.FirstPersonParams = FirstPersonParams;

			FSceneView* View = new FSceneView(ViewInitOptions);
			View->HiddenPrimitives = HiddenPrimitives;
			View->ShowOnlyPrimitives = ShowOnlyPrimitives;

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

			View->ViewLightingChannelMask = PrimaryComponent->ViewLightingChannels.GetMaskForStruct();

			View->bCameraCut = Setup.bCameraCut;

			if (PrimaryComponent->bEnableClipPlane)
			{
				View->GlobalClippingPlane = FPlane(
					PrimaryComponent->ClipPlaneBase,
					PrimaryComponent->ClipPlaneNormal.GetSafeNormal());
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
	ESceneCaptureSource CaptureSource)
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

	// Screen percentage not supported in scene captures.
	ViewFamily.EngineShowFlags.ScreenPercentage = false;
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, /*GlobalResolutionFraction=*/ 1.0f));

	if (PrimaryComponent->IsUnlit())
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
