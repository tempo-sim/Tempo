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
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/WorldSettings.h"
#include "LegacyScreenPercentageDriver.h"
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
	// Mirrors (and multi-view-generalizes) SetupViewFamilyForSceneCapture.
	// Key differences from engine:
	//   - No single SceneCaptureComponent — per-view settings come from Views[I].Component.
	//   - bIsPlanarReflection / CubemapFaceIndex always off.
	//   - bCaptureSceneColor is derived from each view's component (but must be consistent
	//     at the family level; all Tempo tiles use SCS_FinalColorLDR-ish output, same path).
	TArray<FSceneView*> SetupMultiViewFamily(
		FSceneViewFamily& ViewFamily,
		bool bCaptureSceneColor,
		TArrayView<const FViewSetup> Views)
	{
		check(!ViewFamily.GetScreenPercentageInterface());

		ViewFamily.FrameNumber = ViewFamily.Scene->GetFrameNumber();
		ViewFamily.FrameCounter = GFrameCounter;

		TArray<FSceneView*> ViewPtrArray;
		ViewPtrArray.Reserve(Views.Num());

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewSetup& Setup = Views[ViewIndex];
			USceneCaptureComponent2D* Component = Setup.Component;
			check(Component);

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

			// Per-tile view state: critical for TAA / auto-exposure history independence per tile.
			ViewInitOptions.SceneViewStateInterface = Component->GetViewState(0);
			ViewInitOptions.LODDistanceFactor = FMath::Clamp(Component->LODDistanceFactor, .01f, 100.0f);
			ViewInitOptions.bIsSceneCaptureCube = false;
			// Engine's SetupViewFamilyForSceneCapture consults GRayTracingSceneCaptures (a Renderer-
			// private CVar used only for debug overrides: -1 = component, 0 = force off, >0 = force on).
			// We don't link Renderer (see Build.cs note), so just honor the component's own flag.
			ViewInitOptions.bSceneCaptureUsesRayTracing = Component->bUseRayTracingIfEnabled;
			ViewInitOptions.bExcludeFromSceneTextureExtents = Component->bExcludeFromSceneTextureExtents;

			{
				FMinimalViewInfo ViewInfo;
				Component->GetCameraView(0.0f, ViewInfo);
				ViewInitOptions.FirstPersonParams = FFirstPersonParameters(
					ViewInfo.CalculateFirstPersonFOVCorrectionFactor(),
					ViewInfo.FirstPersonScale,
					ViewInfo.bUseFirstPersonParameters);
			}

			FSceneView* View = new FSceneView(ViewInitOptions);

			// Per-tile hide/show.
			{
				TSet<FPrimitiveComponentId> HiddenPrimitives;
				TOptional<TSet<FPrimitiveComponentId>> ShowOnlyPrimitives;

				for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : Component->HiddenComponents)
				{
					if (UPrimitiveComponent* Prim = WeakPrim.Get())
					{
						HiddenPrimitives.Add(Prim->GetPrimitiveSceneId());
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
								HiddenPrimitives.Add(PrimComp->GetPrimitiveSceneId());
							}
						}
					}
				}

				if (Component->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
				{
					ShowOnlyPrimitives.Emplace();
					for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : Component->ShowOnlyComponents)
					{
						if (UPrimitiveComponent* Prim = WeakPrim.Get())
						{
							ShowOnlyPrimitives->Add(Prim->GetPrimitiveSceneId());
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
									ShowOnlyPrimitives->Add(PrimComp->GetPrimitiveSceneId());
								}
							}
						}
					}
				}

				View->HiddenPrimitives = MoveTemp(HiddenPrimitives);
				View->ShowOnlyPrimitives = MoveTemp(ShowOnlyPrimitives);
			}

			ViewFamily.Views.Add(View);
			ViewPtrArray.Add(View);

			View->StartFinalPostprocessSettings(Setup.ViewLocation);

			// Scene-capture default: Lumen disabled unless the component's PPM re-enables it.
			View->FinalPostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
			View->FinalPostProcessSettings.ReflectionMethod = EReflectionMethod::None;
			View->FinalPostProcessSettings.LumenSurfaceCacheResolution = 0.5f;

			View->OverridePostProcessSettings(Component->PostProcessSettings, Component->PostProcessBlendWeight);
			View->EndFinalPostprocessSettings(ViewInitOptions);

			View->ViewLightingChannelMask = Component->ViewLightingChannels.GetMaskForStruct();

			// Tiles reset their own camera-cut flag immediately (they don't own a CaptureScene path
			// that would reset it; we do it here instead).
			View->bCameraCut = Component->bCameraCutThisFrame;
			Component->bCameraCutThisFrame = false;

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
	TArrayView<const FViewSetup> Views)
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

	// CaptureSource is the tiles' setting, not the primary's — the primary in Tempo is the proxy
	// tonemap capture (SCS_FinalColorLDR), while tiles capture HDR (SCS_FinalColorHDR) so the
	// proxy can apply exposure globally. Family-level CaptureSource must match the tiles.
	USceneCaptureComponent2D* CaptureSourceComponent = Views[0].Component;
	const ESceneCaptureSource CaptureSource = CaptureSourceComponent->CaptureSource;
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

	// Component-local view extensions on the primary (tiles typically don't install any).
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

	TArray<FSceneView*> FamilyViews = SetupMultiViewFamily(ViewFamily, bCaptureSceneColor, Views);

	ViewFamily.SceneCaptureSource = CaptureSource;
	ViewFamily.SceneCaptureCompositeMode = CaptureSourceComponent->CompositeMode;

	// Screen percentage not supported in scene captures.
	ViewFamily.EngineShowFlags.ScreenPercentage = false;
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, /*GlobalResolutionFraction=*/ 1.0f));

	if (CaptureSourceComponent->IsUnlit())
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
		USceneCaptureComponent2D* Component = Views[ViewIndex].Component;
		FSceneViewStateInterface* ViewStateInterface = Component->GetViewState(0);
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
