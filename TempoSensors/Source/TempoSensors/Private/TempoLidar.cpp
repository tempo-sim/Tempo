// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoSensors.h"
#include "TempoSensorsConstants.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoLidarParticipatingMediaViewExtension.h"
#include "TempoMultiViewCapture.h"

#include "TempoSensors/Common.pb.h"
#include "TempoSensors/Lidar.pb.h"

#include "TempoSensorsSettings.h"
#include "TempoSensorsTypes.h"
#include "TempoSensorsUtils.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/PerspectiveMatrix.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"
#include "TextureResource.h"

namespace
{
	constexpr int32 LeftTileIndex = 0;
	constexpr int32 CenterTileIndex = 1;
	constexpr int32 RightTileIndex = 2;
}

UTempoLidar::UTempoLidar()
{
	PrimaryComponentTick.bCanEverTick = true;
	MeasurementTypes = { EMeasurementType::LIDAR_SCAN };
	bAutoActivate = true;

	// The lidar primary is never itself captured (it exists only as a container for family-level
	// settings the multi-view helper reads: ShowFlags, hide/show lists, view owner, etc.). Still,
	// match the tile-era CaptureSource so the atlas format is PF_A16B16G16R16 / LDR-compatible.
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	PixelFormatOverride = EPixelFormat::PF_A16B16G16R16;

	// Disable as many unnecessary rendering features as possible for the tile family render.
	OptimizeShowFlagsForNoColor(ShowFlags);
	bUseRayTracingIfEnabled = false;
}

TOptional<TFuture<void>> UTempoLidar::SendMeasurements()
{
	TOptional<TFuture<void>> Future;

	// Snapshot color demand before draining, mirroring TempoCamera's bDepthEnabled flow: if all
	// pending requests opt out of color, we should flip back off. The post-drain queue may be
	// empty so we have to read color intent here.
	const bool bHadColorRequests = [this]
	{
		for (const FLidarScanRequest& Req : PendingRequests)
		{
			if (Req.Request.include_color())
			{
				return true;
			}
		}
		return false;
	}();

	if (TextureReadQueue.NextReadComplete())
	{
		TSharedPtr<FTextureRead> TextureRead = TextureReadQueue.DequeueIfReadComplete();
		const FName ReadType = TextureRead->GetType();
		if (ReadType == TEXT("LidarShared"))
		{
			auto* SharedRead = static_cast<TLidarSharedTextureRead<FLidarPixel>*>(TextureRead.Get());
			TArray<TUniquePtr<FTextureRead>> SliceReads = SharedRead->SplitIntoSlices();
			Future = DecodeAndRespond(MoveTemp(SliceReads), /*bWithColor=*/false);
		}
		else
		{
			check(ReadType == TEXT("LidarColorShared"));
			auto* SharedRead = static_cast<TLidarSharedTextureRead<FLidarPixelWithColor>*>(TextureRead.Get());
			TArray<TUniquePtr<FTextureRead>> SliceReads = SharedRead->SplitIntoSlices();
			Future = DecodeAndRespond(MoveTemp(SliceReads), /*bWithColor=*/true);
		}

		PendingRequests.Empty();
	}

	// Toggle color mode based on client intent. Use the pre-drain snapshot OR'd with any new
	// requests pending (either newly arrived during this function or left over because the last
	// read couldn't satisfy them).
	const bool bColorNeeded = bHadColorRequests || [this]
	{
		for (const FLidarScanRequest& Req : PendingRequests)
		{
			if (Req.Request.include_color())
			{
				return true;
			}
		}
		return false;
	}();
	if (bColorNeeded)
	{
		FramesWithoutColor = 0;
		if (!bColorEnabled)
		{
			SetColorEnabled(true);
		}
	}
	else
	{
		// Debounce the OFF transition: a streaming client briefly has no request pending between
		// receiving a response and re-issuing the next request. Flipping off on that gap and back
		// on the next frame races the RT/staging swap on the render thread (the previous
		// UpdateResource may not have completed) and corrupts in-flight reads.
		++FramesWithoutColor;
		if (bColorEnabled && FramesWithoutColor >= ColorOffDebounceFrames)
		{
			SetColorEnabled(false);
		}
	}

	// Take the opportunity to apply any reconfigure that was detected earlier but had to be
	// deferred because reads were still in flight.
	TryApplyPendingReconfigure();

	return Future;
}

bool UTempoLidar::HasDetectedParameterChange() const
{
	return HorizontalFOV != HorizontalFOV_Internal
		|| VerticalFOV != VerticalFOV_Internal
		|| HorizontalBeams != HorizontalBeams_Internal
		|| VerticalBeams != VerticalBeams_Internal
		|| BeamDivergence != BeamDivergence_Internal
		|| SamplingStrategy != SamplingStrategy_Internal
		|| BeamCalibration != BeamCalibration_Internal
		|| bSimulateParticipatingMedia != bSimulateParticipatingMedia_Internal
		|| MediaRangeBins != MediaRangeBins_Internal;
}

void UTempoLidar::DeactivateAllTiles()
{
	for (FTempoLidarTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			DeactivateTile(Tile);
		}
	}
}

void UTempoLidar::ReconfigureTilesNow()
{
	// Drain all active tiles (releases their view states + PPMs) before re-configuring with new
	// parameters. Caller must have confirmed no reads are in flight.
	DeactivateAllTiles();

	SyncTiles();
	UpdateInternalMirrors();

	// Shared RT geometry depends on the (possibly changed) slice sizes, so rebuild it.
	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitSharedRenderTarget();
	}
}

void UTempoLidar::UpdateInternalMirrors()
{
	HorizontalFOV_Internal = HorizontalFOV;
	VerticalFOV_Internal = VerticalFOV;
	HorizontalBeams_Internal = HorizontalBeams;
	VerticalBeams_Internal = VerticalBeams;
	BeamDivergence_Internal = BeamDivergence;
	SamplingStrategy_Internal = SamplingStrategy;
	BeamCalibration_Internal = BeamCalibration;
	bSimulateParticipatingMedia_Internal = bSimulateParticipatingMedia;
	MediaRangeBins_Internal = MediaRangeBins;
}

void UTempoLidar::OnUnregister()
{
	// Deactivates every tile while the scene is still valid.
	Super::OnUnregister();

	// Unregisters the extension from the engine's list. Render commands still in flight hold their
	// own reference and keep it alive until they have run; its results texture is released on the
	// render thread first.
	if (MediaExtension.IsValid())
	{
		MediaExtension->ReleaseResources();
		MediaExtension.Reset();
	}
	MediaStagingRing.Release();
}

FTempoLidarParticipatingMediaViewExtension* UTempoLidar::GetOrCreateMediaExtension()
{
	if (!MediaExtension.IsValid())
	{
		const UWorld* World = GetWorld();
		if (!GEngine || !World || !World->Scene)
		{
			return nullptr;
		}
		MediaExtension = FSceneViewExtensions::NewExtension<FTempoLidarParticipatingMediaViewExtension>(World->Scene);
	}
	return MediaExtension.Get();
}

#if WITH_EDITOR
void UTempoLidar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, HorizontalFOV) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, VerticalFOV) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, HorizontalBeams) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, VerticalBeams) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, BeamCalibration) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, BeamDivergence) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, SamplingStrategy) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, bSimulateParticipatingMedia) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, MediaRangeBins))
	{
		// Route through the same choke point as the runtime Tick path.
		bReconfigurePending = true;
		TryApplyPendingReconfigure();
	}
}
#endif

FVector2D SphericalToPerspective(double AzimuthDeg, double ElevationDeg)
{
	const double TanAzimuth = FMath::Tan(FMath::DegreesToRadians(AzimuthDeg));
	const double TanElevation = FMath::Tan(FMath::DegreesToRadians(ElevationDeg));
	return FVector2D(TanAzimuth, TanElevation * FMath::Sqrt(TanAzimuth * TanAzimuth + 1));
}

void PerspectiveToSpherical(const FVector2D& PerspectiveImagePlaneLocation, double& AzimuthDeg, double& ElevationDeg)
{
	const double Azimuth = FMath::Atan(PerspectiveImagePlaneLocation.X);
	const double TanAzimuth = FMath::Tan(Azimuth);
	const double Elevation = FMath::Atan(PerspectiveImagePlaneLocation.Y / FMath::Sqrt(TanAzimuth * TanAzimuth + 1));

	AzimuthDeg = FMath::RadiansToDegrees(Azimuth);
	ElevationDeg = FMath::RadiansToDegrees(Elevation);
}

FVector SphericalToCartesian(double AzimuthDeg, double ElevationDeg, double Distance)
{
	const double Azimuth = FMath::DegreesToRadians(AzimuthDeg);
	const double Elevation = FMath::DegreesToRadians(ElevationDeg);
	return Distance * FVector(FMath::Cos(-Elevation) * FMath::Cos(Azimuth), FMath::Cos(-Elevation) * FMath::Sin(Azimuth), FMath::Sin(-Elevation));
}

// ------------------------------------------------------------------------------------
// Tile Management
// ------------------------------------------------------------------------------------

void UTempoLidar::AllocateTileViewState(FTempoLidarTile& Tile)
{
	if (Tile.ViewState.GetReference() == nullptr)
	{
		UWorld* World = GetWorld();
		if (World && World->Scene)
		{
			Tile.ViewState.Allocate(World->Scene->GetFeatureLevel());
		}
	}
}

void UTempoLidar::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UTempoLidar* This = CastChecked<UTempoLidar>(InThis);
	for (FTempoLidarTile& Tile : This->Tiles)
	{
		if (FSceneViewStateInterface* Ref = Tile.ViewState.GetReference())
		{
			Ref->AddReferencedObjects(Collector);
		}
	}
	Super::AddReferencedObjects(InThis, Collector);
}

void UTempoLidar::DeactivateTile(FTempoLidarTile& Tile)
{
	Tile.bActive = false;
	Tile.ViewState.Destroy();
	// Retire the PPM instead of nulling: render commands from prior captures may still reference
	// it, and dropping the only UPROPERTY reference lets GC flag it as "about to be deleted"
	// mid-render (FMaterialRenderProxy::CacheUniformExpressions asserts).
	RetirePPM(Tile.PostProcessMaterialInstance);
	Tile.PostProcessMaterialInstance = nullptr;
	Tile.PostProcessSettings = FPostProcessSettings();
	// Drop the tile's share of the beam table. Any decode still in flight holds its own reference,
	// so this only releases the memory once nothing is using it — and at a few hundred thousand
	// beams that is not a trivial amount to leave attached to a tile that is no longer rendering.
	Tile.BeamSamples.Reset();
	Tile.bCameraCut = false;
}

void UTempoLidar::ApplyLabelOverridesToTiles()
{
	for (FTempoLidarTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			ApplyLabelOverrideParameters(Tile.PostProcessMaterialInstance);
		}
	}
}

void UTempoLidar::ApplyTilePostProcess(FTempoLidarTile& Tile)
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	const TObjectPtr<UMaterialInterface> DesiredMaterial = bColorEnabled
		? TempoSensorsSettings->GetLidarPostProcessMaterialWithColor()
		: TempoSensorsSettings->GetLidarPostProcessMaterial();
	if (!DesiredMaterial)
	{
		UE_LOG(LogTempoSensors, Error, TEXT("Lidar post-process material is not set in TempoSensors settings (bColorEnabled=%d)"), bColorEnabled);
		return;
	}

	// Rebuild the MID if the source material doesn't match (e.g., bColorEnabled toggled). Retire
	// the old MID rather than just dropping the UPROPERTY ref — queued render commands may still
	// hold its FMaterialRenderProxy, and GC collecting it mid-render fatals CacheUniformExpressions.
	if (Tile.PostProcessMaterialInstance && Tile.PostProcessMaterialInstance->Parent != DesiredMaterial)
	{
		RetirePPM(Tile.PostProcessMaterialInstance);
		Tile.PostProcessMaterialInstance = nullptr;
	}

	if (!Tile.PostProcessMaterialInstance)
	{
		Tile.PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(DesiredMaterial.Get(), this);
		Tile.MinDepth = GEngine->NearClipPlane;
		Tile.MaxDepth = TempoSensorsSettings->GetMaxLidarDepth();
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), Tile.MinDepth);
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), Tile.MaxDepth);
		// Both formats now use a 24-bit depth lane: WithColor reserves the top 8 bits of each fp32
		// lane as a NaN/denormal-safety prefix, and no-color gives up its top depth byte to carry
		// reflectivity. Both therefore discretize inverse depth to 2^24 levels.
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempoCamera_Max_Discrete_Depth);
	}

	ApplyLabelOverrideParameters(Tile.PostProcessMaterialInstance);

	Tile.PostProcessMaterialInstance->EnsureIsComplete();

	// In color mode, the photoreal settings (Lumen GI/reflections, MegaLights, AE, etc.) live on
	// THIS component's PostProcessSettings via ApplyPhotorealisticRenderSettings — but the
	// multi-view setup defaults each view to GI = None and only applies the *tile's* PP as an
	// override. So we have to copy the primary's settings into the tile (mirroring TempoCamera's
	// ApplyTilePostProcess pattern) or the per-view render falls back to direct-light-only and
	// shadows show only sky/atmospheric scattering (the "blue shadows" artifact).
	// In no-color mode, none of those overrides are set (OptimizeShowFlagsForNoColor handles the
	// strip-down via show flags instead of PP), so copying is a no-op cost-wise.
	Tile.PostProcessSettings = PostProcessSettings;
	Tile.PostProcessSettings.WeightedBlendables.Array.Empty();
	Tile.PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0, Tile.PostProcessMaterialInstance));
}

double UTempoLidar::GetEffectiveVerticalFOV() const
{
	if (BeamCalibration.Num() > 0)
	{
		float MinElev = TNumericLimits<float>::Max();
		float MaxElev = TNumericLimits<float>::Lowest();
		for (const FLidarBeamCalibration& B : BeamCalibration)
		{
			MinElev = FMath::Min(MinElev, B.ElevationDeg);
			MaxElev = FMath::Max(MaxElev, B.ElevationDeg);
		}
		const float AbsMaxElev = FMath::Max(FMath::Abs(MinElev), FMath::Abs(MaxElev));
		// Pad by one beam-spacing so edge beams land inside the rendered image.
		const float BeamSpacing = (BeamCalibration.Num() > 1)
			? (MaxElev - MinElev) / (BeamCalibration.Num() - 1) : 1.0f;
		return FMath::Max(1.0, static_cast<double>(2.0f * AbsMaxElev + BeamSpacing));
	}
	return VerticalFOV;
}

int32 UTempoLidar::GetEffectiveVerticalBeams() const
{
	return BeamCalibration.Num() > 0 ? BeamCalibration.Num() : VerticalBeams;
}

double UTempoLidar::GetMaxAbsAzimuthOffsetDeg() const
{
	float MaxAbs = 0.0f;
	for (const FLidarBeamCalibration& B : BeamCalibration)
	{
		MaxAbs = FMath::Max(MaxAbs, FMath::Abs(B.AzimuthOffsetDeg));
	}
	return static_cast<double>(MaxAbs);
}

void UTempoLidar::GetOutputElevationRangeDeg(double& MinOut, double& MaxOut) const
{
	if (BeamCalibration.IsEmpty())
	{
		MinOut = -VerticalFOV / 2.0;
		MaxOut = VerticalFOV / 2.0;
		return;
	}
	// Output elevations are negated from the internal ElevationDeg convention, so the most-negative
	// output comes from the most-positive ElevationDeg.
	MinOut = TNumericLimits<double>::Max();
	MaxOut = TNumericLimits<double>::Lowest();
	for (const FLidarBeamCalibration& B : BeamCalibration)
	{
		MinOut = FMath::Min(MinOut, static_cast<double>(-B.ElevationDeg));
		MaxOut = FMath::Max(MaxOut, static_cast<double>(-B.ElevationDeg));
	}
}

// Precompute the per-beam geometry for a tile. None of it depends on anything rendered, only on the
// tile's pixel grid and the beam pattern, so it belongs at configuration time. See FTempoLidarBeamSample.
void UTempoLidar::BuildBeamSamples(FTempoLidarTile& Tile) const
{
	const int32 NumHorizontalBeams = Tile.HorizontalBeams;
	const int32 NumVerticalBeams = GetEffectiveVerticalBeams();
	if (NumHorizontalBeams <= 0 || NumVerticalBeams <= 0 || Tile.SizeXY.X <= 0 || Tile.SizeXY.Y <= 0)
	{
		Tile.BeamSamples.Reset();
		return;
	}

	const double TileVerticalFOV = GetEffectiveVerticalFOV();

	// Mirrors Decode's setup exactly: the mapping between spherical coordinates and the rendered
	// pixel grid has to agree with what the renderer produced.
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(Tile.EffectiveFOVAngle / 2.0, TileVerticalFOV / 2.0);
	const FVector2D SizeXYOffset = (FVector2D(Tile.SizeXY) - Tile.SizeXYFOV) / 2.0;

	auto ImagePlaneLocationToPixelCoordinate = [&](const FVector2D& ImagePlaneLocation)
	{
		return (FVector2D::UnitVector / 2.0 + ImagePlaneLocation / ImagePlaneSize) * (Tile.SizeXYFOV - FVector2D::UnitVector) + SizeXYOffset;
	};
	auto PixelCoordinateToImagePlaneLocation = [&](const FVector2D& PixelCoordinate)
	{
		return ((PixelCoordinate - SizeXYOffset) / (Tile.SizeXYFOV - FVector2D::UnitVector) - (FVector2D::UnitVector / 2.0)) * ImagePlaneSize;
	};

	// Fraction of the FOV, in [-0.5, 0.5], at which beam Index of Count sits. A single beam has no
	// spread to distribute and sits on the tile axis.
	auto Spread = [](int32 Index, int32 Count)
	{
		return Count > 1 ? -0.5 + static_cast<double>(Index) / (Count - 1) : 0.0;
	};

	TArray<FTempoLidarBeamSample> Samples;
	Samples.SetNumUninitialized(NumHorizontalBeams * NumVerticalBeams);

	int32 NumClamped = 0;
	for (int32 HorizontalBeam = 0; HorizontalBeam < NumHorizontalBeams; ++HorizontalBeam)
	{
		const double NominalAzimuthDeg = Spread(HorizontalBeam, NumHorizontalBeams) * Tile.FOVAngle;

		for (int32 VerticalBeam = 0; VerticalBeam < NumVerticalBeams; ++VerticalBeam)
		{
			const bool bCalibrated = BeamCalibration.IsValidIndex(VerticalBeam);
			const double ElevationDeg = bCalibrated
				? BeamCalibration[VerticalBeam].ElevationDeg
				: Spread(VerticalBeam, NumVerticalBeams) * TileVerticalFOV;
			const double AzimuthDeg = NominalAzimuthDeg + (bCalibrated ? BeamCalibration[VerticalBeam].AzimuthOffsetDeg : 0.0);

			const FVector2D PixelCoordinate = ImagePlaneLocationToPixelCoordinate(SphericalToPerspective(AzimuthDeg, ElevationDeg));
			FIntPoint Coord(FMath::RoundToInt32(PixelCoordinate.X), FMath::RoundToInt32(PixelCoordinate.Y));

			// EffectiveFOVAngle is padded so calibrated rays land inside the grid, but clamp rather
			// than trust it: this index is used unchecked in the decode's inner loop, and a beam
			// that falls outside would read off the end of the image.
			const FIntPoint ClampedCoord(
				FMath::Clamp(Coord.X, 0, Tile.SizeXY.X - 1),
				FMath::Clamp(Coord.Y, 0, Tile.SizeXY.Y - 1));
			NumClamped += (ClampedCoord != Coord) ? 1 : 0;
			Coord = ClampedCoord;

			double AzimuthDegNearest, ElevationDegNearest;
			PerspectiveToSpherical(PixelCoordinateToImagePlaneLocation(FVector2D(Coord.X, Coord.Y)), AzimuthDegNearest, ElevationDegNearest);

			FTempoLidarBeamSample& Sample = Samples[HorizontalBeam * NumVerticalBeams + VerticalBeam];
			Sample.PixelIndex = Coord.X + Tile.SizeXY.X * Coord.Y;
			Sample.RayDirectionUnit = FVector3f(SphericalToCartesian(AzimuthDeg, ElevationDeg, 1.0));
			Sample.NearestDirectionUnit = FVector3f(SphericalToCartesian(AzimuthDegNearest, ElevationDegNearest, 1.0));
			Sample.DepthToDistanceDivisor = static_cast<float>(
				FMath::Cos(FMath::DegreesToRadians(AzimuthDegNearest)) * FMath::Cos(FMath::DegreesToRadians(ElevationDegNearest)));
			// Negated from the internal (Unreal-local) convention so that client-side point-cloud
			// math renders in the expected right-handed Z-up frame.
			Sample.AzimuthRad = static_cast<float>(FMath::DegreesToRadians(-(AzimuthDeg + Tile.YawOffset)));
			Sample.ElevationRad = static_cast<float>(FMath::DegreesToRadians(-ElevationDeg));
		}
	}

	if (NumClamped > 0)
	{
		UE_LOG(LogTempoSensors, Warning,
			TEXT("Lidar %s: %d of %d beams fall outside the rendered %dx%d grid and were clamped to its edge. "
				"Their returns will be wrong. This means the tile's padded FOV does not cover the calibrated beam directions."),
			*GetSensorName(), NumClamped, Samples.Num(), Tile.SizeXY.X, Tile.SizeXY.Y);
	}

	Tile.BeamSamples = MakeShared<const TArray<FTempoLidarBeamSample>>(MoveTemp(Samples));
}

void UTempoLidar::ConfigureTile(FTempoLidarTile& Tile, double InYawOffset, double SubHorizontalFOV, int32 SubHorizontalBeams, bool bActivate)
{
	if (!bActivate)
	{
		if (Tile.bActive)
		{
			DeactivateTile(Tile);
		}
		return;
	}

	Tile.YawOffset = InYawOffset;
	Tile.FOVAngle = SubHorizontalFOV;
	Tile.HorizontalBeams = SubHorizontalBeams;

	// Pad the rendered horizontal FOV symmetrically by the worst-case beam AzimuthOffsetDeg so a
	// calibrated ray at the extreme beam still projects inside the pixel grid. Pixel count scales
	// linearly with the FOV expansion to keep angular resolution roughly constant.
	const double MaxAzOffsetDeg = GetMaxAbsAzimuthOffsetDeg();
	Tile.EffectiveFOVAngle = (Tile.FOVAngle > 0.0f)
		? static_cast<float>(Tile.FOVAngle + 2.0 * MaxAzOffsetDeg)
		: Tile.FOVAngle;

	const double EffectiveVertFOV = GetEffectiveVerticalFOV();
	const double UndistortedVerticalImagePlaneSize = 2.0 * FMath::Tan(FMath::DegreesToRadians(EffectiveVertFOV) / 2.0);
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(Tile.EffectiveFOVAngle / 2.0, EffectiveVertFOV / 2.0);

	const double AspectRatio = ImagePlaneSize.Y / ImagePlaneSize.X;

	// Render resolution is driven by beam divergence, not beam count: each pixel subtends roughly
	// one beam-width — the smallest feature the sensor can physically resolve. The pipeline renders
	// square angular pixels: the vertical pixel count follows the FOV aspect ratio so that Decode
	// (which maps via ImagePlaneSize/SizeXYFOV) and the render projection (whose vertical FOV comes
	// from the SizeX/SizeY aspect, see RenderCapture) stay consistent. A single divergence drives
	// both axes; TODO: true independent H/V resolution would require driving the projection's
	// vertical FOV explicitly (e.g. via DistortedVerticalFOV) instead of from the pixel aspect, and
	// matching that in Decode.
	double HorizontalSpan;
	switch (SamplingStrategy)
	{
	case ETempoLidarSamplingStrategy::Conservative:
		// Size for the tile center, where the spherical-to-perspective mapping is coarsest, so no
		// pixel is coarser than a beam-width anywhere. ImagePlaneSize.X spans the (padded) FOV in
		// tangent units; at the center one tangent unit subtends one radian.
		HorizontalSpan = ImagePlaneSize.X / FMath::DegreesToRadians(BeamDivergence);
		break;
	case ETempoLidarSamplingStrategy::Simple:
	default:
		// Size linearly across the FOV: the average pitch is one beam-width, so the center renders
		// slightly coarser and the edges finer.
		HorizontalSpan = Tile.EffectiveFOVAngle / BeamDivergence;
		break;
	}
	Tile.SizeXYFOV = FVector2D(HorizontalSpan, AspectRatio * HorizontalSpan);

	Tile.SizeXY = FIntPoint(FMath::CeilToInt32(Tile.SizeXYFOV.X), FMath::CeilToInt32(Tile.SizeXYFOV.Y));
	Tile.DistortionFactor = UndistortedVerticalImagePlaneSize / ImagePlaneSize.Y;
	Tile.DistortedVerticalFOV = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(EffectiveVertFOV) / 2.0) * Tile.DistortionFactor));

	// After SizeXY / SizeXYFOV are final: the beam-to-pixel mapping depends on the pixel grid.
	BuildBeamSamples(Tile);

	AllocateTileViewState(Tile);
	ApplyTilePostProcess(Tile);

	// First frame with fresh view state must force a camera cut so TAA (if ever enabled) doesn't
	// sample uninitialized history.
	Tile.bCameraCut = !Tile.bActive || Tile.bCameraCut;
	Tile.bActive = true;
}

void UTempoLidar::SyncTiles()
{
	// Tiles copy the family-level settings when configured, so those come first.
	ApplyFamilyRenderSettings();

	FTempoLidarTile& L = Tiles[LeftTileIndex];
	FTempoLidarTile& C = Tiles[CenterTileIndex];
	FTempoLidarTile& R = Tiles[RightTileIndex];

	// We allow up to 120 degrees horizontal FOV per tile.
	if (HorizontalFOV <= 120.0)
	{
		ConfigureTile(L, 0.0, 0.0, 0, false);
		ConfigureTile(C, 0.0, HorizontalFOV, HorizontalBeams, true);
		ConfigureTile(R, 0.0, 0.0, 0, false);
	}
	else if (HorizontalFOV <= 240.0)
	{
		const double BeamGapSize = HorizontalFOV / (HorizontalBeams - 1);
		const int32 LeftSegmentBeams = FMath::CeilToInt32(HorizontalBeams / 2.0);
		const int32 RightSegmentBeams = HorizontalBeams - LeftSegmentBeams;
		const double LeftSegmentFOV = BeamGapSize * (LeftSegmentBeams - 1);
		const double RightSegmentFOV = BeamGapSize * (RightSegmentBeams - 1);
		ConfigureTile(L, -(LeftSegmentFOV + BeamGapSize) / 2.0, LeftSegmentFOV, LeftSegmentBeams, true);
		ConfigureTile(C, 0.0, 0.0, 0, false);
		ConfigureTile(R, (RightSegmentFOV + BeamGapSize) / 2.0, RightSegmentFOV, RightSegmentBeams, true);
	}
	else
	{
		const double BeamGapSize = HorizontalFOV / (HorizontalFOV < 360.0 ? HorizontalBeams - 1 : HorizontalBeams);
		const int32 SideSegmentBeams = FMath::CeilToInt32(HorizontalBeams / 3.0);
		const int32 CenterSegmentBeams = HorizontalBeams - 2 * SideSegmentBeams;
		const double SideSegmentFOV = BeamGapSize * (SideSegmentBeams - 1);
		const double CenterSegmentFOV = BeamGapSize * (CenterSegmentBeams - 1);
		ConfigureTile(L, -BeamGapSize - (CenterSegmentFOV + SideSegmentFOV) / 2.0, SideSegmentFOV, SideSegmentBeams, true);
		ConfigureTile(C, 0.0, CenterSegmentFOV, CenterSegmentBeams, true);
		ConfigureTile(R, BeamGapSize + (CenterSegmentFOV + SideSegmentFOV) / 2.0, SideSegmentFOV, SideSegmentBeams, true);
	}
}

void UTempoLidar::RequestMeasurement(const TempoSensors::LidarScanRequest& Request, const TResponseDelegate<TempoSensors::LidarScanSegment>& ResponseContinuation)
{
	PendingRequests.Add({ Request, ResponseContinuation});
}

void SelectLidarReturns(ETempoLidarReturnMode Mode, const FTempoLidarEcho& Surface, const FTempoLidarEcho& Medium,
	FTempoLidarEcho& OutPrimary, FTempoLidarEcho& OutSecondary)
{
	OutPrimary = FTempoLidarEcho();
	OutSecondary = FTempoLidarEcho();

	if (!Surface.bValid && !Medium.bValid)
	{
		return;
	}
	if (Surface.bValid != Medium.bValid)
	{
		// Only one echo: every mode reports it, and dual mode has no second.
		OutPrimary = Surface.bValid ? Surface : Medium;
		return;
	}

	const FTempoLidarEcho& Nearest = Surface.Distance <= Medium.Distance ? Surface : Medium;
	const FTempoLidarEcho& Farthest = Surface.Distance <= Medium.Distance ? Medium : Surface;
	// Ties go to the surface: it is the echo the sensor would report without media.
	const FTempoLidarEcho& Strongest = Medium.Intensity > Surface.Intensity ? Medium : Surface;
	const FTempoLidarEcho& Weakest = Medium.Intensity > Surface.Intensity ? Surface : Medium;

	switch (Mode)
	{
	case ETempoLidarReturnMode::First:
		OutPrimary = Nearest;
		break;
	case ETempoLidarReturnMode::Last:
		OutPrimary = Farthest;
		break;
	case ETempoLidarReturnMode::Dual:
		OutPrimary = Strongest;
		OutSecondary = Weakest;
		break;
	case ETempoLidarReturnMode::Strongest:
	default:
		OutPrimary = Strongest;
		break;
	}
}

namespace
{
	// Decoder body shared by FLidarPixel and FLidarPixelWithColor specializations. The color branch
	// is constexpr-guarded: the no-color path emits identical proto bytes as before this change.
	template <typename PixelType>
	bool DecodeLidarRead(const TLidarTextureReadBase<PixelType>& Read, float TransmissionTime,
		TempoSensors::LidarScanSegment& ScanSegmentOut)
	{
		const int32 NumReturns = Read.HorizontalBeams * Read.VerticalBeams;

		// Per-beam geometry, precomputed when the tile was configured; see FTempoLidarBeamSample.
		const TArray<FTempoLidarBeamSample>* const BeamSamples = Read.BeamSamples.Get();
		if (!ensureMsgf(BeamSamples && BeamSamples->Num() == NumReturns,
			TEXT("Lidar read has no matching beam sample table (%d entries for %d returns). Dropping scan."),
			BeamSamples ? BeamSamples->Num() : -1, NumReturns))
		{
			return false;
		}

		// Pre-size the packed repeated-scalar output fields so parallel workers can write
		// directly into their contiguous backing storage — no intermediate per-return objects.
		// Azimuths/elevations are per-return to leave room for non-gridded beam patterns.
		// Packed little-endian scalar blobs (float32 for distances/intensities/azimuths/elevations,
		// uint32 for labels). Size each byte buffer once and write directly into its contiguous storage,
		// mirroring the reflectivities/colors path below so workers never touch repeated-field internals.
		std::string* const DistancesOut = ScanSegmentOut.mutable_distances_m();
		DistancesOut->resize(static_cast<size_t>(NumReturns) * sizeof(float));
		float* const DistancesData = reinterpret_cast<float*>(DistancesOut->data());
		std::string* const IntensitiesOut = ScanSegmentOut.mutable_intensities();
		IntensitiesOut->resize(static_cast<size_t>(NumReturns) * sizeof(float));
		float* const IntensitiesData = reinterpret_cast<float*>(IntensitiesOut->data());
		std::string* const LabelsOut = ScanSegmentOut.mutable_labels();
		LabelsOut->resize(static_cast<size_t>(NumReturns) * sizeof(uint32_t));
		uint32_t* const LabelsData = reinterpret_cast<uint32_t*>(LabelsOut->data());
		std::string* const AzimuthsOut = ScanSegmentOut.mutable_azimuths_rad();
		AzimuthsOut->resize(static_cast<size_t>(NumReturns) * sizeof(float));
		float* const AzimuthsData = reinterpret_cast<float*>(AzimuthsOut->data());
		std::string* const ElevationsOut = ScanSegmentOut.mutable_elevations_rad();
		ElevationsOut->resize(static_cast<size_t>(NumReturns) * sizeof(float));
		float* const ElevationsData = reinterpret_cast<float*>(ElevationsOut->data());

		// Reflectivity blob: 1 byte per return, populated in both color and no-color modes. Same
		// H-outer/V-inner layout as the scalar fields above (index h * VerticalBeams + v).
		ScanSegmentOut.mutable_reflectivities()->assign(static_cast<size_t>(NumReturns), '\0');
		char* const ReflectivitiesData = ScanSegmentOut.mutable_reflectivities()->data();

		// Color blob (only allocated for the color variant). Packed 3 bytes per return, indexed
		// 3 * (h * VerticalBeams + v), with byte order set by the project's color image encoding.
		char* ColorsData = nullptr;
		EColorImageEncoding ColorEncoding = EColorImageEncoding::BGR8;
		if constexpr (std::is_same_v<PixelType, FLidarPixelWithColor>)
		{
			std::string* ColorsOut = ScanSegmentOut.mutable_colors();
			ColorsOut->assign(static_cast<size_t>(NumReturns * 3), '\0');
			ColorsData = ColorsOut->data();
			const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
			if (TempoSensorsSettings)
			{
				ColorEncoding = TempoSensorsSettings->GetColorImageEncoding();
			}
			ScanSegmentOut.set_color_encoding(ColorEncoding == EColorImageEncoding::BGR8
				? TempoSensors::ColorEncoding::CE_BGR8
				: TempoSensors::ColorEncoding::CE_RGB8);
		}

		// Participating media: per-pixel results next to the pixel, when they were simulated for
		// this capture. Without them every beam has at most its surface echo.
		const bool bMedia = Read.MediaImage.Num() == Read.Image.Num();
		const FTempoLidarMediaPixel* const MediaPixels = bMedia ? Read.MediaImage.GetData() : nullptr;
		const ETempoLidarReturnMode ReturnMode = Read.ReturnMode;
		const bool bDual = ReturnMode == ETempoLidarReturnMode::Dual;
		const float MinDetectableIntensity = bMedia ? Read.MinDetectableIntensity : 0.0f;
		const uint8 MediumReflectivity = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Read.MediaBackscatter * 255.0f), 0, 255));

		// The second return's arrays exist only in dual mode; same layouts as the first's.
		float* SecondDistancesData = nullptr;
		float* SecondIntensitiesData = nullptr;
		uint32_t* SecondLabelsData = nullptr;
		char* SecondReflectivitiesData = nullptr;
		char* SecondColorsData = nullptr;
		if (bDual)
		{
			TempoSensors::LidarEcho* const Second = ScanSegmentOut.mutable_second_return();
			Second->mutable_distances_m()->resize(static_cast<size_t>(NumReturns) * sizeof(float));
			SecondDistancesData = reinterpret_cast<float*>(Second->mutable_distances_m()->data());
			Second->mutable_intensities()->resize(static_cast<size_t>(NumReturns) * sizeof(float));
			SecondIntensitiesData = reinterpret_cast<float*>(Second->mutable_intensities()->data());
			Second->mutable_labels()->resize(static_cast<size_t>(NumReturns) * sizeof(uint32_t));
			SecondLabelsData = reinterpret_cast<uint32_t*>(Second->mutable_labels()->data());
			Second->mutable_reflectivities()->assign(static_cast<size_t>(NumReturns), '\0');
			SecondReflectivitiesData = Second->mutable_reflectivities()->data();
			if (ColorsData)
			{
				Second->mutable_colors()->assign(static_cast<size_t>(NumReturns * 3), '\0');
				SecondColorsData = Second->mutable_colors()->data();
			}
		}

		// Per-scan constants for the loop below. The incidence test compares cosines, which is the
		// same test as comparing angles (cosine is monotonic over [0, 180] degrees) without an acos
		// per return, and the world-to-sensor normal transform is the inverse rotation and scale
		// taken once rather than inverted per return.
		const double CosMaxAngleOfIncidence = FMath::Cos(FMath::DegreesToRadians(Read.MaxAngleOfIncidence));
		const FQuat InverseCaptureRotation = Read.CaptureTransform.GetRotation().Inverse();
		const FVector InverseCaptureScale = FTransform::GetSafeScaleReciprocal(Read.CaptureTransform.GetScale3D());

		// H-outer, V-inner layout: each ParallelFor iteration owns a contiguous V-length stripe
		// of every output array, so threads never share cache lines.
		ParallelFor(Read.HorizontalBeams, [&Read, BeamSamples, CosMaxAngleOfIncidence, InverseCaptureRotation, InverseCaptureScale,
			DistancesData, IntensitiesData, LabelsData, AzimuthsData, ElevationsData, ReflectivitiesData, ColorsData, ColorEncoding,
			MediaPixels, ReturnMode, bDual, MinDetectableIntensity, MediumReflectivity,
			SecondDistancesData, SecondIntensitiesData, SecondLabelsData, SecondReflectivitiesData, SecondColorsData](int32 HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < Read.VerticalBeams; ++VerticalBeam)
			{
				const FTempoLidarBeamSample& Sample = (*BeamSamples)[HorizontalBeam * Read.VerticalBeams + VerticalBeam];
				const PixelType& Pixel = Read.Image[Sample.PixelIndex];
				// Both pixel formats discretize inverse depth to 24 bits (2^24 levels).
				constexpr float MaxDiscreteDepthValue = GTempoCamera_Max_Discrete_Depth;
				const float NearestDepth = Pixel.Depth(Read.MinDepth, Read.MaxDepth, MaxDiscreteDepthValue);
				const FVector NearestDirection(Sample.NearestDirectionUnit);
				const FVector RayDirectionUnit(Sample.RayDirectionUnit);
				const float NearestDistance = NearestDepth / Sample.DepthToDistanceDivisor;
				const FVector NearestPoint = NearestDirection * NearestDistance;
				const FVector WorldNormal = Pixel.Normal();
				const FVector LocalNormal = InverseCaptureScale * InverseCaptureRotation.RotateVector(WorldNormal);
				const double CosAngleOfIncidence = FVector::DotProduct(LocalNormal.GetSafeNormal(), -RayDirectionUnit);

				// The surface echo, as before participating media: none past the max angle of
				// incidence or the max distance.
				FTempoLidarEcho Surface;
				if (CosAngleOfIncidence >= CosMaxAngleOfIncidence)
				{
					// The intersection depends only on the ray's direction, so the unit vector
					// serves as the second point on the line.
					const FPlane SurfacePlane(NearestPoint, LocalNormal);
					const FVector HitPoint = FMath::LinePlaneIntersection(FVector::ZeroVector, RayDirectionUnit, SurfacePlane);

					const double Distance = HitPoint.Length();
					if (Distance <= Read.MaxDistance)
					{
						Surface.Distance = static_cast<float>(FMath::Max(Read.MinDistance, Distance));
						Surface.Intensity = static_cast<float>(CosAngleOfIncidence * Read.IntensitySaturationDistance / FMath::Max(Read.IntensitySaturationDistance, Distance));
						Surface.bValid = true;
					}
				}

				// Through participating media the surface echo comes back attenuated both ways, and
				// the medium adds an echo of its own.
				FTempoLidarEcho Medium;
				Medium.bMedium = true;
				if (MediaPixels)
				{
					const FTempoLidarMediaPixel& Media = MediaPixels[Sample.PixelIndex];
					const float Transmittance = Media.SurfaceTransmittance();
					Surface.Intensity *= Transmittance * Transmittance;
					if (Surface.Intensity < MinDetectableIntensity)
					{
						Surface.bValid = false;
					}
					if (Media.HasMediumEcho())
					{
						Medium.Distance = Media.MediumRange(static_cast<float>(Read.MaxDistance));
						Medium.Intensity = Media.MediumIntensity();
						Medium.bValid = Medium.Intensity >= MinDetectableIntensity
							&& Medium.Distance >= Read.MinDistance && Medium.Distance <= Read.MaxDistance;
					}
				}

				FTempoLidarEcho Primary;
				FTempoLidarEcho Secondary;
				SelectLidarReturns(ReturnMode, Surface, Medium, Primary, Secondary);

				const int32 Idx = HorizontalBeam * Read.VerticalBeams + VerticalBeam;
				// Already negated into the client's right-handed frame, and offset by the tile yaw.
				AzimuthsData[Idx] = Sample.AzimuthRad;
				ElevationsData[Idx] = Sample.ElevationRad;

				// A medium echo has no surface behind it to label, and its reflectivity is the
				// medium's backscatter; its color, like the surface's, is what the pixel rendered.
				auto WriteEcho = [&](const FTempoLidarEcho& Echo, float* Distances, float* Intensities, uint32_t* Labels, char* Reflectivities, char* Colors)
				{
					Distances[Idx] = Echo.bValid ? QuantityConverter<CM2M>::Convert(Echo.Distance) : 0.0f;
					Intensities[Idx] = Echo.bValid ? Echo.Intensity : 0.0f;
					// For a non-return (Distance == 0) the label and reflectivity are meaningless but
					// harmless, mirroring how colors are written unconditionally.
					Labels[Idx] = Echo.bMedium ? 0u : Pixel.Label();
					Reflectivities[Idx] = static_cast<char>(Echo.bMedium ? MediumReflectivity : Pixel.ReflectivityByte());
					if constexpr (std::is_same_v<PixelType, FLidarPixelWithColor>)
					{
						char* const ColorOut = Colors + Idx * 3;
						if (ColorEncoding == EColorImageEncoding::BGR8)
						{
							ColorOut[0] = static_cast<char>(Pixel.B());
							ColorOut[1] = static_cast<char>(Pixel.G());
							ColorOut[2] = static_cast<char>(Pixel.R());
						}
						else
						{
							ColorOut[0] = static_cast<char>(Pixel.R());
							ColorOut[1] = static_cast<char>(Pixel.G());
							ColorOut[2] = static_cast<char>(Pixel.B());
						}
					}
				};
				WriteEcho(Primary, DistancesData, IntensitiesData, LabelsData, ReflectivitiesData, ColorsData);
				if (bDual)
				{
					WriteEcho(Secondary, SecondDistancesData, SecondIntensitiesData, SecondLabelsData, SecondReflectivitiesData, SecondColorsData);
				}
			}
		});

		switch (ReturnMode)
		{
		case ETempoLidarReturnMode::First:
			ScanSegmentOut.set_return_mode(TempoSensors::LidarReturnMode::LRM_FIRST);
			break;
		case ETempoLidarReturnMode::Last:
			ScanSegmentOut.set_return_mode(TempoSensors::LidarReturnMode::LRM_LAST);
			break;
		case ETempoLidarReturnMode::Dual:
			ScanSegmentOut.set_return_mode(TempoSensors::LidarReturnMode::LRM_DUAL);
			break;
		case ETempoLidarReturnMode::Strongest:
		default:
			ScanSegmentOut.set_return_mode(TempoSensors::LidarReturnMode::LRM_STRONGEST);
			break;
		}

		Read.ExtractMeasurementHeader(TransmissionTime, ScanSegmentOut.mutable_header());

		ScanSegmentOut.set_scan_count(Read.NumCaptureComponents);
		ScanSegmentOut.set_horizontal_beams(Read.HorizontalBeams);
		ScanSegmentOut.set_vertical_beams(Read.VerticalBeams);
		// Distances are emitted in meters (see CM2M conversion above), so the range is too.
		ScanSegmentOut.mutable_distance_range_m()->set_min(QuantityConverter<CM2M>::Convert(Read.MinDistance));
		ScanSegmentOut.mutable_distance_range_m()->set_max(QuantityConverter<CM2M>::Convert(Read.MaxDistance));
		ScanSegmentOut.mutable_azimuth_range_rad()->set_max(FMath::DegreesToRadians(Read.HorizontalFOV / 2.0 + Read.RelativeYaw));
		ScanSegmentOut.mutable_azimuth_range_rad()->set_min(FMath::DegreesToRadians(-Read.HorizontalFOV / 2.0 + Read.RelativeYaw));
		ScanSegmentOut.mutable_elevation_range_rad()->set_max(FMath::DegreesToRadians(Read.MaxOutputElevationDeg));
		ScanSegmentOut.mutable_elevation_range_rad()->set_min(FMath::DegreesToRadians(Read.MinOutputElevationDeg));

		return true;
	}
}

bool TTextureRead<FLidarPixel>::Decode(float TransmissionTime, TempoSensors::LidarScanSegment& ScanSegmentOut) const
{
	return DecodeLidarRead(*this, TransmissionTime, ScanSegmentOut);
}

bool TTextureRead<FLidarPixelWithColor>::Decode(float TransmissionTime, TempoSensors::LidarScanSegment& ScanSegmentOut) const
{
	return DecodeLidarRead(*this, TransmissionTime, ScanSegmentOut);
}

TFuture<void> UTempoLidar::DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads, bool bWithColor)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		this,
		TextureReads = MoveTemp(TextureReads),
		Requests = PendingRequests,
		TransmissionTimeCpy = TransmissionTime,
		bWithColor
		]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecodeAndRespond);

		TArray<TempoSensors::LidarScanSegment> Segments;
		Segments.SetNum(TextureReads.Num());
		// A slice that fails to decode is withheld rather than sent empty, so a client counting
		// segments per sweep sees a missing one, not a malformed one.
		TArray<bool> Decoded;
		Decoded.SetNumZeroed(TextureReads.Num());
		if (!Requests.IsEmpty())
		{
			ParallelFor(TextureReads.Num(), [&TextureReads, &Segments, &Decoded, TransmissionTimeCpy, bWithColor](int Index)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecode);
				if (bWithColor)
				{
					Decoded[Index] = static_cast<TTextureRead<FLidarPixelWithColor>*>(TextureReads[Index].Get())->Decode(TransmissionTimeCpy, Segments[Index]);
				}
				else
				{
					Decoded[Index] = static_cast<TTextureRead<FLidarPixel>*>(TextureReads[Index].Get())->Decode(TransmissionTimeCpy, Segments[Index]);
				}
			});
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarRespond);
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			if (!Decoded[Index])
			{
				continue;
			}
			for (const auto& Request : Requests)
			{
				Request.ResponseContinuation.ExecuteIfBound(Segments[Index], grpc::Status_OK);
			}
		}
	});

	return Future;
}

void UTempoLidar::SetColorEnabled(bool bColorEnabledIn)
{
	if (bColorEnabled == bColorEnabledIn)
	{
		return;
	}

	UE_LOG(LogTempoSensors, Display, TEXT("Setting owner: %s lidar: %s color enabled: %d"), *GetOwnerName(), *GetSensorName(), bColorEnabledIn);

	bColorEnabled = bColorEnabledIn;
	ApplyColorEnabled();
}

void UTempoLidar::ApplyFamilyRenderSettings()
{
	// Color mode uses Lumen + ray tracing + the full tonemap/AE/show-flag set so the WithColor PPM
	// samples a realistically lit scene; the no-color path strips those out for the fast aux-only
	// render. Reset the PostProcessSettings and ShowFlags blocks before reapplying so toggling off
	// cleanly drops the Lumen/MegaLights overrides set by the helper.
	PostProcessSettings = FPostProcessSettings();
	ShowFlags = FEngineShowFlags(ESFIM_Game);
	if (bColorEnabled)
	{
		ApplyPhotorealisticRenderSettings(PostProcessSettings, ShowFlags, bUseRayTracingIfEnabled);
		// TSR's sub-pixel jitter is great for full-screen rendering but produces visible
		// per-frame jitter in lidar returns — each beam reads a single pixel and TSR shifts
		// which pixel that is across frames. Temporal accumulation isn't needed for a sensor
		// that integrates once per beam, so turn it off (the camera helper turned it on).
		ShowFlags.SetTemporalAA(false);
		ShowFlags.SetAntiAliasing(false);
	}
	else
	{
		OptimizeShowFlagsForNoColor(ShowFlags);
		bUseRayTracingIfEnabled = false;
	}

	if (bSimulateParticipatingMedia)
	{
		// The media profile is built from the fog the camera would render, read off the renderer's
		// per-view fog constants and, for volumetric fog, its froxel grid. Both exist only when fog
		// renders for the family, so render it (the no-color PPM ignores the resulting color).
		ShowFlags.SetFog(true);
		ShowFlags.SetVolumetricFog(true);
	}
}

void UTempoLidar::ApplyColorEnabled()
{
	ApplyFamilyRenderSettings();

	// Swap each active tile's PPM to the matching variant. ApplyTilePostProcess retires the old
	// MID if its parent material doesn't match the bColorEnabled-selected one.
	for (FTempoLidarTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			ApplyTilePostProcess(Tile);
			// Force a camera cut on the next render: TAA history from the previous render mode
			// is invalid because the post-process pipeline (and exposure) is being swapped.
			Tile.bCameraCut = true;
		}
	}

	// The atlas pixel format depends on bColorEnabled. Any in-flight reads reference the old
	// format's staging textures, so drop them and rebuild the shared RT + staging ring at the
	// new size. This is the same drain-and-reinit path the depth toggle takes on the camera.
	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitSharedRenderTarget();
	}
}

// ------------------------------------------------------------------------------------
// Shared RT / capture timer
// ------------------------------------------------------------------------------------

void UTempoLidar::InitRenderTarget()
{
	// The lidar primary never renders its own CaptureScene (ShouldManageOwnReadback / Timer both
	// return false); SharedTextureTarget is the only RT it manages. Do NOT call Super::InitRenderTarget
	// because that would allocate the inherited TextureTarget + staging textures we never use.
	InitSharedRenderTarget();
}

void UTempoLidar::InitSharedRenderTarget()
{
	// Walk active slices in Left->Center->Right order, assigning each its horizontal offset
	// within the packed RT. Packed width is the running sum; packed height is the max slice height.
	int32 PackedX = 0;
	int32 MaxY = 0;
	for (FTempoLidarTile& Tile : Tiles)
	{
		if (!Tile.bActive)
		{
			continue;
		}
		Tile.SliceDestOffsetX = PackedX;
		PackedX += Tile.SizeXY.X;
		MaxY = FMath::Max(MaxY, Tile.SizeXY.Y);
	}

	if (PackedX <= 0 || MaxY <= 0)
	{
		return;
	}

	// Drop pending reads BEFORE swapping the RT pointer, not after. Otherwise the render thread
	// can pick up OnRenderCompleted between the SharedTextureTarget swap and the queue Empty(),
	// iterate stale in-flight reads (still pointing at old 8B staging), and feed them the new 16B
	// RT — Read() then issues CopyTexture(NEW_RT, OLD_STAGING) with mismatched per-pixel sizes and
	// the subsequent map+memcpy crashes in _platform_memmove. Empty()-then-swap closes that window;
	// the captured TSharedPtr in the pending render-command closure still keeps the old RT+staging
	// pair alive together until it runs.
	TextureReadQueue.Empty();

	SharedTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedTextureTarget->TargetGamma = GetDefault<UTempoSensorsSettings>()->GetSceneCaptureGamma();
	SharedTextureTarget->bGPUSharedFlag = true;
	if (bColorEnabled)
	{
		// 4x float32 lanes per pixel, used as raw 32-bit byte carriers. The WithColor PPM writes
		// asfloat(packed_uint) into each lane; FLidarPixelWithColor reads the same 16 bytes back
		// as uint32[4]. UTextureRenderTarget2D::IsSupportedFormat rejects PF_R32G32B32A32_UINT for
		// PPM render targets, so we use the float variant — same stride, same memory pattern.
		SharedTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA32f;
		SharedTextureTarget->InitCustomFormat(PackedX, MaxY, EPixelFormat::PF_A32B32G32R32F, true);
	}
	else
	{
		SharedTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		SharedTextureTarget->InitCustomFormat(PackedX, MaxY, EPixelFormat::PF_A16B16G16R16, true);
	}

	AllocateStagingTextures(SharedTextureTarget->SizeX, SharedTextureTarget->SizeY, SharedTextureTarget->GetFormat());

	// The media results are read back through their own ring, sized like the atlas so every tile's
	// view rect indexes both the same way.
	if (bSimulateParticipatingMedia)
	{
		MediaStagingRing.Allocate(GetName() + TEXT(" Media"), GetNumStagingTextures(), SharedTextureTarget->SizeX, SharedTextureTarget->SizeY,
			FTempoLidarParticipatingMediaViewExtension::ResultsFormat);
	}
	else
	{
		MediaStagingRing.Release();
		if (MediaExtension.IsValid())
		{
			MediaExtension->ReleaseResources();
		}
	}
}

int32 UTempoLidar::GetNumActiveTiles() const
{
	int32 Count = 0;
	for (const FTempoLidarTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			++Count;
		}
	}
	return Count;
}

void UTempoLidar::RenderCapture()
{
	// SharedTextureTarget and its resource are validated by the base MaybeMarkPendingCapture, but
	// we need the resource pointer here so the render command closure can capture it by value.
	FTextureRenderTargetResource* SharedRTResource = SharedTextureTarget->GameThread_GetRenderTargetResource();

	UWorld* World = GetWorld();
	FSceneInterface* Scene = World ? World->Scene : nullptr;
	if (!Scene)
	{
		return;
	}

	// Like the camera, the lidar renders its tiles via RenderTiles' own FSceneRenderer and never
	// calls UpdateSceneCaptureContents, so expand FRayTracingScene's readback rings here before
	// rendering or the engine-default ring of 4 overruns once several sensors capture per frame.
	EnsureRayTracingReadbackBuffersExpanded(Scene);

	// The no-color lidar renders with ray tracing off (bUseRayTracingIfEnabled = false, Lumen forced to
	// None per view), so without this the render's EndFrame() would delete the readback buffers the main
	// viewport's ray-tracing scene still has copies in flight against. Also enqueued before RenderTiles.
	PinRayTracingSceneUsedThisFrame(Scene);

	// Per-tile view origin (shared across tiles) — the lidar's world location.
	const FTransform LidarWorld = GetComponentToWorld();
	const FVector ViewLocation = LidarWorld.GetTranslation();
	const FQuat LidarWorldRotation = LidarWorld.GetRotation();

	// Axis swap for UE view rotation convention: view x = world z, view y = world x, view z = world y.
	const FMatrix ViewAxisSwap(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	// Build per-tile view setups and per-slice reads. Each tile's ViewRect inside the atlas is
	// (SliceDestOffsetX, 0) to (SliceDestOffsetX + SizeXY.X, SizeXY.Y), matching the pack layout
	// FLidarSharedTextureRead::SplitIntoSlices expects.
	const int32 NumActiveTiles = GetNumActiveTiles();
	double MinOutputElevationDeg, MaxOutputElevationDeg;
	GetOutputElevationRangeDeg(MinOutputElevationDeg, MaxOutputElevationDeg);
	TArray<TempoMultiViewCapture::FViewSetup> ViewSetups;
	ViewSetups.Reserve(NumActiveTiles);

	TArray<TUniquePtr<TTextureRead<FLidarPixel>>> Slices;
	TArray<TUniquePtr<TTextureRead<FLidarPixelWithColor>>> SlicesWithColor;
	if (bColorEnabled)
	{
		SlicesWithColor.Reserve(NumActiveTiles);
	}
	else
	{
		Slices.Reserve(NumActiveTiles);
	}

	const double CaptureTime = World->GetTimeSeconds();

	// Participating media are simulated by a view extension gathered into this render only, whose
	// results the read picks up next to the atlas. The staging ring is allocated with the atlas;
	// without it (a reconfigure not yet applied) render without media rather than pair the read with
	// nothing.
	FTempoLidarParticipatingMediaViewExtension* MediaExt = bSimulateParticipatingMedia ? GetOrCreateMediaExtension() : nullptr;
	const bool bMedia = MediaExt != nullptr && MediaStagingRing.IsValid(FTempoLidarParticipatingMediaViewExtension::ResultsFormat);

	int32 PackedX = 0;
	int32 MaxY = 0;
	for (FTempoLidarTile& Tile : Tiles)
	{
		if (!Tile.bActive)
		{
			continue;
		}

		// Tile world rotation = lidar world rotation * tile yaw; view matrix is the inverse
		// quaternion followed by the capture-view axis swap.
		const FQuat TileWorldRotation = LidarWorldRotation * FRotator(0.0, Tile.YawOffset, 0.0).Quaternion();
		FMatrix ViewRotationMatrix = FQuatRotationMatrix(TileWorldRotation.Inverse()) * ViewAxisSwap;

		// Perspective projection matching the tile's SizeXY + EffectiveFOVAngle. EffectiveFOVAngle
		// is the padded beam FOV (covers AzimuthOffsetDeg); the rendered pixel grid must span at
		// least this range so calibrated rays don't fall outside it. Near-clip from the primary's
		// override (or the engine default) — lidar tiles historically inherit this.
		const float UnscaledFOV = Tile.EffectiveFOVAngle * (float)PI / 360.0f;
		const float ViewFOV = FMath::Atan((1.0f + Overscan) * FMath::Tan(UnscaledFOV));
		const float NearClip = bOverride_CustomNearClippingPlane ? CustomNearClippingPlane : GNearClippingPlane;
		const FIntPoint TileSize = Tile.SizeXY;
		const float YAxisMultiplier = static_cast<float>(TileSize.X) / static_cast<float>(TileSize.Y);

		FMatrix ProjectionMatrix;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
		// ERHIZBuffer was removed in 5.8; an inverted (reversed) Z buffer is now always assumed.
		if (/* DISABLES CODE */ (true))
#else
		if ((int32)ERHIZBuffer::IsInverted)
#endif
		{
			ProjectionMatrix = FReversedZPerspectiveMatrix(ViewFOV, ViewFOV, 1.0f, YAxisMultiplier, NearClip, NearClip);
		}
		else
		{
			ProjectionMatrix = FPerspectiveMatrix(ViewFOV, ViewFOV, 1.0f, YAxisMultiplier, NearClip, NearClip);
		}

		TempoMultiViewCapture::FViewSetup& Setup = ViewSetups.AddDefaulted_GetRef();
		Setup.ViewState = Tile.ViewState.GetReference();
		Setup.PostProcessSettings = &Tile.PostProcessSettings;
		Setup.PostProcessBlendWeight = 1.0f;
		Setup.bCameraCut = Tile.bCameraCut;
		Tile.bCameraCut = false;
		Setup.ViewLocation = ViewLocation;
		Setup.ViewRotationMatrix = ViewRotationMatrix;
		Setup.ProjectionMatrix = ProjectionMatrix;
		Setup.ViewRect = FIntRect(FIntPoint(Tile.SliceDestOffsetX, 0), FIntPoint(Tile.SliceDestOffsetX + TileSize.X, TileSize.Y));
		Setup.FOV = Tile.FOVAngle;

		// Build the per-slice read with the metadata its Decode path expects.
		const FTransform TileWorldTransform(TileWorldRotation, ViewLocation);
		auto BuildSlice = [&]<typename P>(TArray<TUniquePtr<TTextureRead<P>>>& Out)
		{
			TTextureRead<P>* Slice = new TTextureRead<P>(
				Tile.SizeXY, SequenceId, CaptureTime, GetOwnerName(), GetSensorName(),
				GetComponentTransform(), TileWorldTransform, Tile.FOVAngle,
				Tile.HorizontalBeams, GetEffectiveVerticalBeams(),
				MinOutputElevationDeg, MaxOutputElevationDeg,
				IntensitySaturationDistance, MaxAngleOfIncidence,
				NumActiveTiles, Tile.YawOffset, Tile.MinDepth, Tile.MaxDepth,
				MinDistance, MaxDistance, Tile.BeamSamples);
			Slice->ReturnMode = ReturnMode;
			Slice->MinDetectableIntensity = MinDetectableIntensity;
			Slice->MediaBackscatter = MediaBackscatter;
			if (bMedia)
			{
				Slice->MediaImage.SetNumUninitialized(Tile.SizeXY.X * Tile.SizeXY.Y);
			}
			Out.Emplace(Slice);
		};
		if (bColorEnabled)
		{
			BuildSlice.template operator()<FLidarPixelWithColor>(SlicesWithColor);
		}
		else
		{
			BuildSlice.template operator()<FLidarPixel>(Slices);
		}

		PackedX = FMath::Max(PackedX, Tile.SliceDestOffsetX + Tile.SizeXY.X);
		MaxY = FMath::Max(MaxY, Tile.SizeXY.Y);
	}

	if (bMedia)
	{
		FTempoLidarMediaCaptureSetup Setup;
		Setup.ResultsSize = FIntPoint(SharedTextureTarget->SizeX, SharedTextureTarget->SizeY);
		Setup.Sensor.NumBins = MediaRangeBins;
		// Returns closer than this are already the sensor's blind spot, so the first bin ends there.
		Setup.Sensor.FirstBinEdge = static_cast<float>(FMath::Max(MinDistance, 50.0));
		Setup.Sensor.MaxRange = static_cast<float>(MaxDistance);
		Setup.Sensor.ExtinctionScale = MediaExtinctionScale;
		Setup.Sensor.Backscatter = MediaBackscatter;
		Setup.Sensor.IntensitySaturationDistance = static_cast<float>(IntensitySaturationDistance);
		Setup.Sensor.bStochastic = bStochasticMediaReturns;
		Setup.Sensor.Seed = static_cast<uint32>(SequenceId);
		Setup.bIncludeTranslucency = bMediaIncludesTranslucency;
		MediaExt->SetCaptureSetup(Setup);
	}

	// Render all views in one family directly into SharedTextureTarget. The media extension is
	// gathered only into this family: active around the render, nothing else can gather it.
	if (bMedia)
	{
		MediaExt->SetActive(true);
	}
	TempoMultiViewCapture::RenderTiles(Scene, this, SharedTextureTarget, ViewSetups, ESceneCaptureSource::SCS_FinalColorLDR);
	if (bMedia)
	{
		MediaExt->SetActive(false);
	}

	TSharedPtr<FTextureRead> NewRead;
	if (bColorEnabled)
	{
		NewRead = MakeShared<TLidarSharedTextureRead<FLidarPixelWithColor>>(
			FIntPoint(PackedX, MaxY), SequenceId, CaptureTime, GetOwnerName(), GetSensorName(),
			GetComponentTransform(), MoveTemp(SlicesWithColor));
	}
	else
	{
		NewRead = MakeShared<TLidarSharedTextureRead<FLidarPixel>>(
			FIntPoint(PackedX, MaxY), SequenceId, CaptureTime, GetOwnerName(), GetSensorName(),
			GetComponentTransform(), MoveTemp(Slices));
	}

	NewRead->StagingTexture = AcquireNextStagingTexture();

	if (bMedia)
	{
		// The media results go to their own staging texture, copied behind the render and ahead of
		// the atlas copy, whose fence then covers both.
		const FTextureRHIRef MediaStaging = MediaStagingRing.AcquireNext();
		auto SetMediaStaging = [&]<typename P>()
		{
			auto* SharedRead = static_cast<TLidarSharedTextureRead<P>*>(NewRead.Get());
			SharedRead->MediaStagingTexture = MediaStaging;
			SharedRead->MediaImage.SetNumUninitialized(PackedX * MaxY);
		};
		if (bColorEnabled)
		{
			SetMediaStaging.template operator()<FLidarPixelWithColor>();
		}
		else
		{
			SetMediaStaging.template operator()<FLidarPixel>();
		}
		TSharedRef<FTempoLidarParticipatingMediaViewExtension, ESPMode::ThreadSafe> MediaExtRef = MediaExtension.ToSharedRef();
		ENQUEUE_RENDER_COMMAND(TempoLidarMediaStagingCopy)([MediaExtRef, MediaStaging](FRHICommandListImmediate& RHICmdList)
		{
			MediaExtRef->CopyResultsToStaging_RenderThread(RHICmdList, MediaStaging);
		});
	}

	SequenceId++;

	// Single copy from the packed atlas to staging — the atlas IS the packed output.
	FTextureRead::EnqueueStagingCopy(NewRead, SharedRTResource);

	TextureReadQueue.Enqueue(MoveTemp(NewRead));
}

template <typename PixelType>
FName TLidarSharedTextureRead<PixelType>::GetType() const
{
	if constexpr (std::is_same_v<PixelType, FLidarPixel>)
	{
		return TEXT("LidarShared");
	}
	else
	{
		return TEXT("LidarColorShared");
	}
}

template <typename PixelType>
void TLidarSharedTextureRead<PixelType>::ReadAdditional_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	if (MediaImage.IsEmpty())
	{
		return;
	}
	// The atlas read has just waited on the fence behind both copies, so no fence is needed here.
	if (!MediaStagingTexture.IsValid() || !FTextureRead::StagingMatches(MediaStagingTexture, this->ImageSize, sizeof(FTempoLidarMediaPixel)))
	{
		UE_LOG(LogTempoSensors, Warning, TEXT("Skipping lidar media read: no matching staging texture. Reporting no media for this scan."));
		FMemory::Memzero(MediaImage.GetData(), MediaImage.Num() * sizeof(FTempoLidarMediaPixel));
		return;
	}
	FTextureRead::CopyStagingSurface(RHICmdList, MediaStagingTexture, nullptr, reinterpret_cast<uint8*>(MediaImage.GetData()), this->ImageSize, sizeof(FTempoLidarMediaPixel));
}

template <typename PixelType>
TArray<TUniquePtr<FTextureRead>> TLidarSharedTextureRead<PixelType>::SplitIntoSlices()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarSplitIntoSlices);

	TArray<TUniquePtr<FTextureRead>> Result;
	Result.Reserve(Slices.Num());

	const bool bMedia = !MediaImage.IsEmpty();
	int32 RunningX = 0;
	for (TUniquePtr<TTextureRead<PixelType>>& Slice : Slices)
	{
		const FIntPoint SliceSize = Slice->ImageSize;
		const int32 SrcX = RunningX;
		const int32 PackedWidth = this->ImageSize.X;
		TTextureRead<PixelType>* SlicePtr = Slice.Get();
		TArray<PixelType>& PackedImage = this->Image;
		TArray<FTempoLidarMediaPixel>& PackedMedia = MediaImage;
		// A slice built without a media image (media enabled after the slice was built) gets none.
		const bool bSliceMedia = bMedia && SlicePtr->MediaImage.Num() == SliceSize.X * SliceSize.Y;
		if (bMedia && !bSliceMedia)
		{
			SlicePtr->MediaImage.Empty();
		}
		ParallelFor(SliceSize.Y, [SlicePtr, &PackedImage, &PackedMedia, bSliceMedia, SrcX, PackedWidth, SliceSize](int32 Row)
		{
			FMemory::Memcpy(
				&SlicePtr->Image[Row * SliceSize.X],
				&PackedImage[Row * PackedWidth + SrcX],
				SliceSize.X * sizeof(PixelType));
			if (bSliceMedia)
			{
				FMemory::Memcpy(
					&SlicePtr->MediaImage[Row * SliceSize.X],
					&PackedMedia[Row * PackedWidth + SrcX],
					SliceSize.X * sizeof(FTempoLidarMediaPixel));
			}
		});
		RunningX += SliceSize.X;
		Result.Emplace(Slice.Release());
	}
	Slices.Empty();
	return Result;
}

template struct TLidarSharedTextureRead<FLidarPixel>;
template struct TLidarSharedTextureRead<FLidarPixelWithColor>;
