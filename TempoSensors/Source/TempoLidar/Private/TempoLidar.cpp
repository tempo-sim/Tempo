// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoSensorsConstants.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoLabelTypes.h"
#include "TempoLidarModule.h"
#include "TempoMultiViewCapture.h"

#include "TempoLidar/Lidar.pb.h"

#include "TempoSensorsSettings.h"
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

	if (TextureReadQueue.NextReadComplete())
	{
		TUniquePtr<FTextureRead> TextureRead = TextureReadQueue.DequeueIfReadComplete();
		check(TextureRead->GetType() == TEXT("LidarShared"));
		FLidarSharedTextureRead* SharedRead = static_cast<FLidarSharedTextureRead*>(TextureRead.Get());
		TArray<TUniquePtr<FTextureRead>> SliceReads = SharedRead->SplitIntoSlices();
		Future = DecodeAndRespond(MoveTemp(SliceReads));

		PendingRequests.Empty();
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
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoLidar, BeamCalibration))
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

	if (!Tile.PostProcessMaterialInstance)
	{
		if (const TObjectPtr<UMaterialInterface> PostProcessMaterial = TempoSensorsSettings->GetLidarPostProcessMaterial())
		{
			Tile.PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterial.Get(), this);
			Tile.MinDepth = GEngine->NearClipPlane;
			Tile.MaxDepth = TempoSensorsSettings->GetMaxLidarDepth();
			Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), Tile.MinDepth);
			Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), Tile.MaxDepth);
			Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempo_Max_Discrete_Depth);
		}
		else
		{
			UE_LOG(LogTempoLidar, Error, TEXT("LidarPostProcessMaterial is not set in TempoSensors settings"));
			return;
		}
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

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	Tile.YawOffset = InYawOffset;
	Tile.FOVAngle = SubHorizontalFOV;
	Tile.HorizontalBeams = SubHorizontalBeams;

	const double EffectiveVertFOV = GetEffectiveVerticalFOV();
	const double UndistortedVerticalImagePlaneSize = 2.0 * FMath::Tan(EffectiveVertFOV / 2.0);
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(Tile.FOVAngle / 2.0, EffectiveVertFOV / 2.0);

	const double AspectRatio = ImagePlaneSize.Y / ImagePlaneSize.X;
	Tile.SizeXYFOV = TempoSensorsSettings->GetLidarUpsamplingFactor() * FVector2D(Tile.HorizontalBeams, AspectRatio * Tile.HorizontalBeams);

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

void UTempoLidar::RequestMeasurement(const TempoLidar::LidarScanRequest& Request, const TResponseDelegate<TempoLidar::LidarScanSegment>& ResponseContinuation)
{
	PendingRequests.Add({ Request, ResponseContinuation});
}

void TTextureRead<FLidarPixel>::Decode(float TransmissionTime, TempoLidar::LidarScanSegment& ScanSegmentOut) const
{
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(HorizontalFOV / 2.0, VerticalFOV / 2.0);
	const FVector2D SizeXYOffset = (FVector2D(ImageSize) - SizeXYFOV) / 2.0;

	const int32 NumReturns = HorizontalBeams * VerticalBeams;

	// Pre-size the packed repeated-scalar output fields so parallel workers can write
	// directly into their contiguous backing storage — no intermediate per-return objects.
	// Azimuths/elevations are per-return to leave room for non-gridded beam patterns.
	ScanSegmentOut.mutable_distances()->Resize(NumReturns, 0.0f);
	ScanSegmentOut.mutable_intensities()->Resize(NumReturns, 0.0f);
	ScanSegmentOut.mutable_labels()->Resize(NumReturns, 0u);
	ScanSegmentOut.mutable_azimuths()->Resize(NumReturns, 0.0f);
	ScanSegmentOut.mutable_elevations()->Resize(NumReturns, 0.0f);
	float* const DistancesData = ScanSegmentOut.mutable_distances()->mutable_data();
	float* const IntensitiesData = ScanSegmentOut.mutable_intensities()->mutable_data();
	uint32_t* const LabelsData = ScanSegmentOut.mutable_labels()->mutable_data();
	float* const AzimuthsData = ScanSegmentOut.mutable_azimuths()->mutable_data();
	float* const ElevationsData = ScanSegmentOut.mutable_elevations()->mutable_data();

	// H-outer, V-inner layout: each ParallelFor iteration owns a contiguous V-length stripe
	// of every output array, so threads never share cache lines.
	ParallelFor(HorizontalBeams, [this, &ImagePlaneSize, &SizeXYOffset,
		DistancesData, IntensitiesData, LabelsData, AzimuthsData, ElevationsData](int32 HorizontalBeam)
	{
		auto ImagePlaneLocationToPixelCoordinate = [this, &ImagePlaneSize, &SizeXYOffset](const FVector2D& ImagePlaneLocation)
		{
			return (FVector2D::UnitVector / 2.0 + ImagePlaneLocation / ImagePlaneSize) * (SizeXYFOV - FVector2D::UnitVector) + SizeXYOffset;
		};

		auto PixelCoordinateToImagePlaneLocation = [this, &ImagePlaneSize, &SizeXYOffset](const FVector2D& PixelCoordinate)
		{
			return ((PixelCoordinate - SizeXYOffset) / (SizeXYFOV - FVector2D::UnitVector) - (FVector2D::UnitVector / 2.0)) * ImagePlaneSize;
		};

		for (int32 VerticalBeam = 0; VerticalBeam < VerticalBeams; ++VerticalBeam)
		{
			const double NominalAzimuthDeg = (-0.5 + static_cast<double>(HorizontalBeam) / (HorizontalBeams - 1)) * HorizontalFOV;
			const bool bCalibrated = BeamCalibration.IsValidIndex(VerticalBeam);
			const double ElevationDeg = bCalibrated
				? BeamCalibration[VerticalBeam].ElevationDeg
				: (-0.5 + static_cast<double>(VerticalBeam) / (VerticalBeams - 1)) * VerticalFOV;
			const double AzimuthDeg = NominalAzimuthDeg + (bCalibrated ? BeamCalibration[VerticalBeam].AzimuthOffsetDeg : 0.0);
			const FVector2D ImagePlaneLocation = SphericalToPerspective(AzimuthDeg, ElevationDeg);
			const FVector2D PixelCoordinate = ImagePlaneLocationToPixelCoordinate(ImagePlaneLocation);
			const FIntPoint Coord(FMath::RoundToInt32(PixelCoordinate.X), FMath::RoundToInt32(PixelCoordinate.Y));
			const float NearestDepth = Image[Coord.X + ImageSize.X * Coord.Y].Depth(MinDepth, MaxDepth, GTempo_Max_Discrete_Depth);
			const FVector2D ImagePlaneLocationAboveLeft = PixelCoordinateToImagePlaneLocation(FVector2D(Coord.X, Coord.Y));
			double AzimuthDegNearest, ElevationDegNearest;
			PerspectiveToSpherical(ImagePlaneLocationAboveLeft, AzimuthDegNearest, ElevationDegNearest);
			const float NearestDistance = DepthToDistance(AzimuthDegNearest, ElevationDegNearest, NearestDepth);
			const FVector NearestPoint = SphericalToCartesian(AzimuthDegNearest, ElevationDegNearest, NearestDistance);
			const FVector WorldNormal = Image[Coord.X + ImageSize.X * Coord.Y].Normal();
			const FVector LocalNormal = CaptureTransform.InverseTransformVector(WorldNormal);
			const FVector RayDirection = SphericalToCartesian(AzimuthDeg, ElevationDeg, MaxDistance);
			const double CosAngleOfIncidence = FVector::DotProduct(LocalNormal.GetSafeNormal(), -RayDirection.GetSafeNormal());
			const double AngleOfIncidence = FMath::RadiansToDegrees(FMath::Acos(CosAngleOfIncidence));
			double Intensity;
			double Distance;
			if (AngleOfIncidence > MaxAngleOfIncidence)
			{
				Distance = 0.0;
				Intensity = 0.0;
			}
			else
			{
				const FPlane SurfacePlane(NearestPoint, LocalNormal);
				const FVector HitPoint = FMath::LinePlaneIntersection(FVector::ZeroVector, RayDirection, SurfacePlane);

				Distance = HitPoint.Length();
				Intensity = CosAngleOfIncidence * IntensitySaturationDistance / FMath::Max(IntensitySaturationDistance, Distance);

				if (Distance > MaxDistance)
				{
					Distance = 0.0;
					Intensity = 0.0;
				}
				Distance = FMath::Max(MinDistance, Distance);
			}

			const int32 Idx = HorizontalBeam * VerticalBeams + VerticalBeam;
			DistancesData[Idx] = QuantityConverter<CM2M>::Convert(Distance);
			IntensitiesData[Idx] = static_cast<float>(Intensity);
			LabelsData[Idx] = Image[Coord.X + ImageSize.X * Coord.Y].Label();
			// Negated from the internal (Unreal-local) convention so that client-side
			// point-cloud math renders in the expected right-handed Z-up frame.
			AzimuthsData[Idx] = static_cast<float>(FMath::DegreesToRadians(-(AzimuthDeg + RelativeYaw)));
			ElevationsData[Idx] = static_cast<float>(FMath::DegreesToRadians(-ElevationDeg));
		}
	});

	ExtractMeasurementHeader(TransmissionTime, ScanSegmentOut.mutable_header());

	ScanSegmentOut.set_scan_count(NumCaptureComponents);
	ScanSegmentOut.set_horizontal_beams(HorizontalBeams);
	ScanSegmentOut.set_vertical_beams(VerticalBeams);
	ScanSegmentOut.mutable_azimuth_range()->set_max(FMath::DegreesToRadians(HorizontalFOV / 2.0 + RelativeYaw));
	ScanSegmentOut.mutable_azimuth_range()->set_min(FMath::DegreesToRadians(-HorizontalFOV / 2.0 + RelativeYaw));
	if (BeamCalibration.Num() > 0)
	{
		// Output elevations are negated from the internal ElevationDeg convention, so the range
		// is also negated: the most-negative output comes from the most-positive ElevationDeg.
		float MinOutputElev = TNumericLimits<float>::Max();
		float MaxOutputElev = TNumericLimits<float>::Lowest();
		for (const FLidarBeamCalibration& B : BeamCalibration)
		{
			const float OutputElev = -B.ElevationDeg;
			MinOutputElev = FMath::Min(MinOutputElev, OutputElev);
			MaxOutputElev = FMath::Max(MaxOutputElev, OutputElev);
		}
		ScanSegmentOut.mutable_elevation_range()->set_max(FMath::DegreesToRadians(MaxOutputElev));
		ScanSegmentOut.mutable_elevation_range()->set_min(FMath::DegreesToRadians(MinOutputElev));
	}
	else
	{
		ScanSegmentOut.mutable_elevation_range()->set_max(FMath::DegreesToRadians(VerticalFOV / 2.0));
		ScanSegmentOut.mutable_elevation_range()->set_min(FMath::DegreesToRadians(-VerticalFOV / 2.0));
	}
}

TFuture<void> UTempoLidar::DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		this,
		TextureReads = MoveTemp(TextureReads),
		Requests = PendingRequests,
		TransmissionTimeCpy = TransmissionTime
		]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecodeAndRespond);

		TArray<TempoLidar::LidarScanSegment> Segments;
		Segments.SetNum(TextureReads.Num());
		if (!Requests.IsEmpty())
		{
			ParallelFor(TextureReads.Num(), [&TextureReads, &Segments, TransmissionTimeCpy](int Index)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecode);
				static_cast<TTextureRead<FLidarPixel>*>(TextureReads[Index].Get())->Decode(TransmissionTimeCpy, Segments[Index]);
			});
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarRespond);
		for (const TempoLidar::LidarScanSegment& Segment : Segments)
		{
			for (const auto& Request : Requests)
			{
				Request.ResponseContinuation.ExecuteIfBound(Segment, grpc::Status_OK);
			}
		}
	});

	return Future;
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

	SharedTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedTextureTarget->TargetGamma = GetDefault<UTempoSensorsSettings>()->GetSceneCaptureGamma();
	SharedTextureTarget->bGPUSharedFlag = true;
	SharedTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	SharedTextureTarget->InitCustomFormat(PackedX, MaxY, EPixelFormat::PF_A16B16G16R16, true);

	AllocateStagingTextures(SharedTextureTarget->SizeX, SharedTextureTarget->SizeY, SharedTextureTarget->GetFormat());

	// Any pending texture reads reference the previous RT geometry; discard them.
	TextureReadQueue.Empty();
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
	Slices.Reserve(NumActiveTiles);

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

		// Perspective projection matching the tile's SizeXY + FOVAngle. Near-clip from the primary's
		// override (or the engine default) — lidar tiles historically inherit this.
		const float UnscaledFOV = Tile.FOVAngle * (float)PI / 360.0f;
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
		Slices.Emplace(new TTextureRead<FLidarPixel>(
			Tile.SizeXY, SequenceId, CaptureTime, GetOwnerName(), GetSensorName(),
			GetComponentTransform(), TileWorldTransform, Tile.FOVAngle,
			GetEffectiveVerticalFOV(), Tile.HorizontalBeams, GetEffectiveVerticalBeams(), Tile.SizeXYFOV,
			IntensitySaturationDistance, MaxAngleOfIncidence,
			NumActiveTiles, Tile.YawOffset, Tile.MinDepth, Tile.MaxDepth,
			MinDistance, MaxDistance, BeamCalibration));

		PackedX = FMath::Max(PackedX, Tile.SliceDestOffsetX + Tile.SizeXY.X);
		MaxY = FMath::Max(MaxY, Tile.SizeXY.Y);
	}

	// Render all views in one family directly into SharedTextureTarget.
	TempoMultiViewCapture::RenderTiles(Scene, this, SharedTextureTarget, ViewSetups, ESceneCaptureSource::SCS_FinalColorLDR);

	FLidarSharedTextureRead* NewRead = new FLidarSharedTextureRead(
		FIntPoint(PackedX, MaxY), SequenceId, CaptureTime, GetOwnerName(), GetSensorName(),
		GetComponentTransform(), MoveTemp(Slices));

	NewRead->StagingTexture = AcquireNextStagingTexture();

	SequenceId++;

	const FTextureRHIRef StagingTex = NewRead->StagingTexture;

	// Single copy from the packed atlas to staging — the atlas IS the packed output.
	ENQUEUE_RENDER_COMMAND(TempoLidarStagingCopy)(
		[SharedRTResource, StagingTex, NewRead](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SharedRT = SharedRTResource->GetRenderTargetTexture();

			RHICmdList.Transition(FRHITransitionInfo(SharedRT, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			RHICmdList.CopyTexture(SharedRT, StagingTex, FRHICopyTextureInfo());

			NewRead->RenderFence = RHICreateGPUFence(TEXT("TempoLidarRenderFence"));
			RHICmdList.WriteGPUFence(NewRead->RenderFence);
		});

	TextureReadQueue.Enqueue(NewRead);
}

TArray<TUniquePtr<FTextureRead>> FLidarSharedTextureRead::SplitIntoSlices()
{
	TArray<TUniquePtr<FTextureRead>> Result;
	Result.Reserve(Slices.Num());

	int32 RunningX = 0;
	for (TUniquePtr<TTextureRead<FLidarPixel>>& Slice : Slices)
	{
		const FIntPoint SliceSize = Slice->ImageSize;
		const int32 SrcX = RunningX;
		const int32 PackedWidth = ImageSize.X;
		TTextureRead<FLidarPixel>* SlicePtr = Slice.Get();
		TArray<FLidarPixel>& PackedImage = Image;
		ParallelFor(SliceSize.Y, [SlicePtr, &PackedImage, SrcX, PackedWidth, SliceSize](int32 Row)
		{
			FMemory::Memcpy(
				&SlicePtr->Image[Row * SliceSize.X],
				&PackedImage[Row * PackedWidth + SrcX],
				SliceSize.X * sizeof(FLidarPixel));
		});
		RunningX += SliceSize.X;
		Result.Emplace(Slice.Release());
	}
	Slices.Empty();
	return Result;
}
