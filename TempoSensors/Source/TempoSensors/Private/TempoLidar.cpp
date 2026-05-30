// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoSensors.h"
#include "TempoSensorsConstants.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoLabelTypes.h"
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
		|| BeamCalibration != BeamCalibration_Internal;
}

void UTempoLidar::ReconfigureTilesNow()
{
	// Drain all active tiles (releases their view states + PPMs) before re-configuring with new
	// parameters. Caller must have confirmed no reads are in flight.
	for (FTempoLidarTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			DeactivateTile(Tile);
		}
	}

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
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, SamplingStrategy))
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

double DepthToDistance(double AzimuthDeg, double ElevationDeg, double Depth)
{
	const double CosAzimuth = FMath::Cos(FMath::DegreesToRadians(AzimuthDeg));
	const double CosElevation = FMath::Cos(FMath::DegreesToRadians(ElevationDeg));
	return Depth / (CosAzimuth * CosElevation);
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
	Tile.bCameraCut = false;
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
		// WithColor depth lane is 24 bits (the float-bit-cast format reserves the top 8 bits as a
		// NaN/denormal-safety prefix). No-color depth lane is the full 32 bits.
		const float MaxDiscreteDepthForPPM = bColorEnabled
			? GTempoCamera_Max_Discrete_Depth
			: GTempo_Max_Discrete_Depth;
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), MaxDiscreteDepthForPPM);
	}

	// Optional label overrides.
	UDataTable* SemanticLabelTable = TempoSensorsSettings->GetSemanticLabelTable();
	const FName OverridableLabelRowName = TempoSensorsSettings->GetOverridableLabelRowName();
	const FName OverridingLabelRowName = TempoSensorsSettings->GetOverridingLabelRowName();
	TOptional<int32> OverridableLabel;
	TOptional<int32> OverridingLabel;
	if (SemanticLabelTable && !OverridableLabelRowName.IsNone())
	{
		SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""),
			[&OverridableLabelRowName, &OverridingLabelRowName, &OverridableLabel, &OverridingLabel]
			(const FName& Key, const FSemanticLabel& Value)
			{
				if (Key == OverridableLabelRowName)
				{
					OverridableLabel = Value.Label;
				}
				if (Key == OverridingLabelRowName)
				{
					OverridingLabel = Value.Label;
				}
			});
	}

	if (OverridableLabel.IsSet() && OverridingLabel.IsSet())
	{
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridableLabel"), OverridableLabel.GetValue());
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), OverridingLabel.GetValue());
	}
	else
	{
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), 0.0);
	}

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

	AllocateTileViewState(Tile);
	ApplyTilePostProcess(Tile);

	// First frame with fresh view state must force a camera cut so TAA (if ever enabled) doesn't
	// sample uninitialized history.
	Tile.bCameraCut = !Tile.bActive || Tile.bCameraCut;
	Tile.bActive = true;
}

void UTempoLidar::SyncTiles()
{
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

namespace
{
	// Decoder body shared by FLidarPixel and FLidarPixelWithColor specializations. The color branch
	// is constexpr-guarded: the no-color path emits identical proto bytes as before this change.
	template <typename PixelType>
	void DecodeLidarRead(const TLidarTextureReadBase<PixelType>& Read, float TransmissionTime,
		TempoSensors::LidarScanSegment& ScanSegmentOut)
	{
		// EffectiveHorizontalFOV is the padded FOV the renderer actually produced (covers
		// nominal beam FOV + AzimuthOffsetDeg). HorizontalFOV is the unpadded beam FOV and is only
		// used for the per-beam nominal azimuth formula and the reported azimuth range.
		const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(Read.EffectiveHorizontalFOV / 2.0, Read.VerticalFOV / 2.0);
		const FVector2D SizeXYOffset = (FVector2D(Read.ImageSize) - Read.SizeXYFOV) / 2.0;

		const int32 NumReturns = Read.HorizontalBeams * Read.VerticalBeams;

		// Pre-size the packed repeated-scalar output fields so parallel workers can write
		// directly into their contiguous backing storage — no intermediate per-return objects.
		// Azimuths/elevations are per-return to leave room for non-gridded beam patterns.
		ScanSegmentOut.mutable_distances_m()->Resize(NumReturns, 0.0f);
		ScanSegmentOut.mutable_intensities()->Resize(NumReturns, 0.0f);
		ScanSegmentOut.mutable_labels()->Resize(NumReturns, 0u);
		ScanSegmentOut.mutable_azimuths_rad()->Resize(NumReturns, 0.0f);
		ScanSegmentOut.mutable_elevations_rad()->Resize(NumReturns, 0.0f);
		float* const DistancesData = ScanSegmentOut.mutable_distances_m()->mutable_data();
		float* const IntensitiesData = ScanSegmentOut.mutable_intensities()->mutable_data();
		uint32_t* const LabelsData = ScanSegmentOut.mutable_labels()->mutable_data();
		float* const AzimuthsData = ScanSegmentOut.mutable_azimuths_rad()->mutable_data();
		float* const ElevationsData = ScanSegmentOut.mutable_elevations_rad()->mutable_data();

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

		// H-outer, V-inner layout: each ParallelFor iteration owns a contiguous V-length stripe
		// of every output array, so threads never share cache lines.
		ParallelFor(Read.HorizontalBeams, [&Read, &ImagePlaneSize, &SizeXYOffset,
			DistancesData, IntensitiesData, LabelsData, AzimuthsData, ElevationsData, ColorsData, ColorEncoding](int32 HorizontalBeam)
		{
			auto ImagePlaneLocationToPixelCoordinate = [&Read, &ImagePlaneSize, &SizeXYOffset](const FVector2D& ImagePlaneLocation)
			{
				return (FVector2D::UnitVector / 2.0 + ImagePlaneLocation / ImagePlaneSize) * (Read.SizeXYFOV - FVector2D::UnitVector) + SizeXYOffset;
			};

			auto PixelCoordinateToImagePlaneLocation = [&Read, &ImagePlaneSize, &SizeXYOffset](const FVector2D& PixelCoordinate)
			{
				return ((PixelCoordinate - SizeXYOffset) / (Read.SizeXYFOV - FVector2D::UnitVector) - (FVector2D::UnitVector / 2.0)) * ImagePlaneSize;
			};

			for (int32 VerticalBeam = 0; VerticalBeam < Read.VerticalBeams; ++VerticalBeam)
			{
				const double NominalAzimuthDeg = (-0.5 + static_cast<double>(HorizontalBeam) / (Read.HorizontalBeams - 1)) * Read.HorizontalFOV;
				const bool bCalibrated = Read.BeamCalibration.IsValidIndex(VerticalBeam);
				const double ElevationDeg = bCalibrated
					? Read.BeamCalibration[VerticalBeam].ElevationDeg
					: (-0.5 + static_cast<double>(VerticalBeam) / (Read.VerticalBeams - 1)) * Read.VerticalFOV;
				const double AzimuthDeg = NominalAzimuthDeg + (bCalibrated ? Read.BeamCalibration[VerticalBeam].AzimuthOffsetDeg : 0.0);
				const FVector2D ImagePlaneLocation = SphericalToPerspective(AzimuthDeg, ElevationDeg);
				const FVector2D PixelCoordinate = ImagePlaneLocationToPixelCoordinate(ImagePlaneLocation);
				const FIntPoint Coord(FMath::RoundToInt32(PixelCoordinate.X), FMath::RoundToInt32(PixelCoordinate.Y));
				const PixelType& Pixel = Read.Image[Coord.X + Read.ImageSize.X * Coord.Y];
				constexpr float MaxDiscreteDepthValue = std::is_same_v<PixelType, FLidarPixelWithColor>
					? GTempoCamera_Max_Discrete_Depth
					: GTempo_Max_Discrete_Depth;
				const float NearestDepth = Pixel.Depth(Read.MinDepth, Read.MaxDepth, MaxDiscreteDepthValue);
				const FVector2D ImagePlaneLocationAboveLeft = PixelCoordinateToImagePlaneLocation(FVector2D(Coord.X, Coord.Y));
				double AzimuthDegNearest, ElevationDegNearest;
				PerspectiveToSpherical(ImagePlaneLocationAboveLeft, AzimuthDegNearest, ElevationDegNearest);
				const float NearestDistance = DepthToDistance(AzimuthDegNearest, ElevationDegNearest, NearestDepth);
				const FVector NearestPoint = SphericalToCartesian(AzimuthDegNearest, ElevationDegNearest, NearestDistance);
				const FVector WorldNormal = Pixel.Normal();
				const FVector LocalNormal = Read.CaptureTransform.InverseTransformVector(WorldNormal);
				const FVector RayDirection = SphericalToCartesian(AzimuthDeg, ElevationDeg, Read.MaxDistance);
				const double CosAngleOfIncidence = FVector::DotProduct(LocalNormal.GetSafeNormal(), -RayDirection.GetSafeNormal());
				const double AngleOfIncidence = FMath::RadiansToDegrees(FMath::Acos(CosAngleOfIncidence));
				double Intensity;
				double Distance;
				if (AngleOfIncidence > Read.MaxAngleOfIncidence)
				{
					Distance = 0.0;
					Intensity = 0.0;
				}
				else
				{
					const FPlane SurfacePlane(NearestPoint, LocalNormal);
					const FVector HitPoint = FMath::LinePlaneIntersection(FVector::ZeroVector, RayDirection, SurfacePlane);

					Distance = HitPoint.Length();
					Intensity = CosAngleOfIncidence * Read.IntensitySaturationDistance / FMath::Max(Read.IntensitySaturationDistance, Distance);

					if (Distance > Read.MaxDistance)
					{
						Distance = 0.0;
						Intensity = 0.0;
					}
					Distance = FMath::Max(Read.MinDistance, Distance);
				}

				const int32 Idx = HorizontalBeam * Read.VerticalBeams + VerticalBeam;
				DistancesData[Idx] = QuantityConverter<CM2M>::Convert(Distance);
				IntensitiesData[Idx] = static_cast<float>(Intensity);
				LabelsData[Idx] = Pixel.Label();
				// Negated from the internal (Unreal-local) convention so that client-side
				// point-cloud math renders in the expected right-handed Z-up frame.
				AzimuthsData[Idx] = static_cast<float>(FMath::DegreesToRadians(-(AzimuthDeg + Read.RelativeYaw)));
				ElevationsData[Idx] = static_cast<float>(FMath::DegreesToRadians(-ElevationDeg));

				if constexpr (std::is_same_v<PixelType, FLidarPixelWithColor>)
				{
					char* const ColorOut = ColorsData + Idx * 3;
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
			}
		});

		Read.ExtractMeasurementHeader(TransmissionTime, ScanSegmentOut.mutable_header());

		ScanSegmentOut.set_scan_count(Read.NumCaptureComponents);
		ScanSegmentOut.set_horizontal_beams(Read.HorizontalBeams);
		ScanSegmentOut.set_vertical_beams(Read.VerticalBeams);
		// Distances are emitted in meters (see CM2M conversion above), so the range is too.
		ScanSegmentOut.mutable_distance_range_m()->set_min(QuantityConverter<CM2M>::Convert(Read.MinDistance));
		ScanSegmentOut.mutable_distance_range_m()->set_max(QuantityConverter<CM2M>::Convert(Read.MaxDistance));
		ScanSegmentOut.mutable_azimuth_range_rad()->set_max(FMath::DegreesToRadians(Read.HorizontalFOV / 2.0 + Read.RelativeYaw));
		ScanSegmentOut.mutable_azimuth_range_rad()->set_min(FMath::DegreesToRadians(-Read.HorizontalFOV / 2.0 + Read.RelativeYaw));
		if (Read.BeamCalibration.Num() > 0)
		{
			// Output elevations are negated from the internal ElevationDeg convention, so the range
			// is also negated: the most-negative output comes from the most-positive ElevationDeg.
			float MinOutputElev = TNumericLimits<float>::Max();
			float MaxOutputElev = TNumericLimits<float>::Lowest();
			for (const FLidarBeamCalibration& B : Read.BeamCalibration)
			{
				const float OutputElev = -B.ElevationDeg;
				MinOutputElev = FMath::Min(MinOutputElev, OutputElev);
				MaxOutputElev = FMath::Max(MaxOutputElev, OutputElev);
			}
			ScanSegmentOut.mutable_elevation_range_rad()->set_max(FMath::DegreesToRadians(MaxOutputElev));
			ScanSegmentOut.mutable_elevation_range_rad()->set_min(FMath::DegreesToRadians(MinOutputElev));
		}
		else
		{
			ScanSegmentOut.mutable_elevation_range_rad()->set_max(FMath::DegreesToRadians(Read.VerticalFOV / 2.0));
			ScanSegmentOut.mutable_elevation_range_rad()->set_min(FMath::DegreesToRadians(-Read.VerticalFOV / 2.0));
		}
	}
}

void TTextureRead<FLidarPixel>::Decode(float TransmissionTime, TempoSensors::LidarScanSegment& ScanSegmentOut) const
{
	DecodeLidarRead(*this, TransmissionTime, ScanSegmentOut);
}

void TTextureRead<FLidarPixelWithColor>::Decode(float TransmissionTime, TempoSensors::LidarScanSegment& ScanSegmentOut) const
{
	DecodeLidarRead(*this, TransmissionTime, ScanSegmentOut);
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
		if (!Requests.IsEmpty())
		{
			ParallelFor(TextureReads.Num(), [&TextureReads, &Segments, TransmissionTimeCpy, bWithColor](int Index)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecode);
				if (bWithColor)
				{
					static_cast<TTextureRead<FLidarPixelWithColor>*>(TextureReads[Index].Get())->Decode(TransmissionTimeCpy, Segments[Index]);
				}
				else
				{
					static_cast<TTextureRead<FLidarPixel>*>(TextureReads[Index].Get())->Decode(TransmissionTimeCpy, Segments[Index]);
				}
			});
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarRespond);
		for (const TempoSensors::LidarScanSegment& Segment : Segments)
		{
			for (const auto& Request : Requests)
			{
				Request.ResponseContinuation.ExecuteIfBound(Segment, grpc::Status_OK);
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

void UTempoLidar::ApplyColorEnabled()
{
	// Reapply the family-level rendering config. Color mode uses Lumen + ray tracing + the full
	// tonemap/AE/show-flag set so the WithColor PPM samples a realistically lit scene; the
	// no-color path strips those out for the fast aux-only render. Reset the PostProcessSettings
	// and ShowFlags blocks before reapplying so toggling off cleanly drops the Lumen/MegaLights
	// overrides set by the helper.
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
		if ((int32)ERHIZBuffer::IsInverted)
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
			Out.Emplace(new TTextureRead<P>(
				Tile.SizeXY, SequenceId, CaptureTime, GetOwnerName(), GetSensorName(),
				GetComponentTransform(), TileWorldTransform, Tile.FOVAngle, Tile.EffectiveFOVAngle,
				GetEffectiveVerticalFOV(), Tile.HorizontalBeams, GetEffectiveVerticalBeams(), Tile.SizeXYFOV,
				IntensitySaturationDistance, MaxAngleOfIncidence,
				NumActiveTiles, Tile.YawOffset, Tile.MinDepth, Tile.MaxDepth,
				MinDistance, MaxDistance, BeamCalibration));
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

	// Render all views in one family directly into SharedTextureTarget.
	TempoMultiViewCapture::RenderTiles(Scene, this, SharedTextureTarget, ViewSetups, ESceneCaptureSource::SCS_FinalColorLDR);

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

	SequenceId++;

	const FTextureRHIRef StagingTex = NewRead->StagingTexture;

	// Single copy from the packed atlas to staging — the atlas IS the packed output. The lambda
	// holds a TSharedPtr to NewRead so a concurrent TextureReadQueue.Empty() on the game thread
	// (e.g., from InitSharedRenderTarget during a reconfigure) can't free the read before this
	// command runs.
	ENQUEUE_RENDER_COMMAND(TempoLidarStagingCopy)(
		[SharedRTResource, StagingTex, NewRead](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SharedRT = SharedRTResource->GetRenderTargetTexture();

			RHICmdList.Transition(FRHITransitionInfo(SharedRT, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			RHICmdList.CopyTexture(SharedRT, StagingTex, FRHICopyTextureInfo());

			NewRead->RenderFence = RHICreateGPUFence(TEXT("TempoLidarRenderFence"));
			RHICmdList.WriteGPUFence(NewRead->RenderFence);

			// Force the fence's commands to the RHI thread now. Otherwise WriteGPUFence is only
			// recorded into the immediate list and isn't submitted until end-of-frame — which never
			// runs in fixed-step/non-pipelined mode, where OnRenderCompleted parks the render thread
			// inside OnEndFrameRT spinning on this very fence (ReadAllAvailable bBlock=true). Without
			// this flush the signalling commands sit unsubmitted behind the thread waiting on them.
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		});

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
TArray<TUniquePtr<FTextureRead>> TLidarSharedTextureRead<PixelType>::SplitIntoSlices()
{
	TArray<TUniquePtr<FTextureRead>> Result;
	Result.Reserve(Slices.Num());

	int32 RunningX = 0;
	for (TUniquePtr<TTextureRead<PixelType>>& Slice : Slices)
	{
		const FIntPoint SliceSize = Slice->ImageSize;
		const int32 SrcX = RunningX;
		const int32 PackedWidth = this->ImageSize.X;
		TTextureRead<PixelType>* SlicePtr = Slice.Get();
		TArray<PixelType>& PackedImage = this->Image;
		ParallelFor(SliceSize.Y, [SlicePtr, &PackedImage, SrcX, PackedWidth, SliceSize](int32 Row)
		{
			FMemory::Memcpy(
				&SlicePtr->Image[Row * SliceSize.X],
				&PackedImage[Row * PackedWidth + SrcX],
				SliceSize.X * sizeof(PixelType));
		});
		RunningX += SliceSize.X;
		Result.Emplace(Slice.Release());
	}
	Slices.Empty();
	return Result;
}

template struct TLidarSharedTextureRead<FLidarPixel>;
template struct TLidarSharedTextureRead<FLidarPixelWithColor>;
