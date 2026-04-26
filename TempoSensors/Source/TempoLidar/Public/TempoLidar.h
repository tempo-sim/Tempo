// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoTiledSceneCaptureComponent.h"

#include "TempoLidar/Lidar.pb.h"

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "SceneTypes.h"
#include "TempoScriptingServer.h"
#include "TempoLidar.generated.h"

// 8-byte pixel format where first 3 bytes are normal, 4th byte is label.
// 5th-8th bytes are a uint32 representing discrete depth.
struct FLidarPixel
{
	static constexpr bool bSupportsDepth = false;

	FVector Normal() const
	{
		return FVector( 2.0 * U1 / 255.0 - 1.0, 2.0 * U2 / 255.0 - 1.0, 2.0 * U3 / 255.0 - 1.0 );
	}

	uint8 Label() const { return U4; }

	float Depth(float MinDepth, float MaxDepth, float MaxDiscretizedDepth) const
	{
		// We discretize inverse depth to give more consistent precision vs depth.
		// See https://developer.nvidia.com/content/depth-precision-visualized
		const float InverseDepthFraction = static_cast<float>(U5) / MaxDiscretizedDepth;
		const float InverseDepth = InverseDepthFraction * (1.0 / MinDepth - 1.0 / MaxDepth) + 1.0 / MaxDepth;
		return 1.0 / InverseDepth;
	}

private:
	uint8 U1 = 0;
	uint8 U2 = 0;
	uint8 U3 = 0;
	uint8 U4 = 0;
	uint32 U5 = 0;
};

struct FLidarScanRequest
{
	TempoLidar::LidarScanRequest Request;
	TResponseDelegate<TempoLidar::LidarScanSegment> ResponseContinuation;
};

// Intrinsic calibration for a single beam channel, matching vendor calibration file conventions.
USTRUCT(BlueprintType)
struct FLidarBeamCalibration
{
	GENERATED_BODY()

	// Elevation angle for this beam channel in degrees. Positive = up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=-90.0, UIMax=90.0, ClampMin=-90.0, ClampMax=90.0))
	float ElevationDeg = 0.0f;

	// Fixed azimuth offset from the nominal spin angle for this channel in degrees.
	// Corresponds to "rotCorrection" / "horizOffsetCorrection" in vendor calibration files.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=-5.0, UIMax=5.0))
	float AzimuthOffsetDeg = 0.0f;

	bool operator==(const FLidarBeamCalibration& Other) const
	{
		return ElevationDeg == Other.ElevationDeg && AzimuthOffsetDeg == Other.AzimuthOffsetDeg;
	}
	bool operator!=(const FLidarBeamCalibration& Other) const { return !(*this == Other); }
};

class UTempoLidar;

// Per-tile state for the multi-view atlas render. A tile is just a view-rect + view state + PPM; no
// USceneCaptureComponent and no child scene component. The atlas render is driven from
// UTempoLidar::MaybeCapture via TempoMultiViewCapture::RenderTiles, which writes each view directly
// into its rect inside SharedTextureTarget — no per-tile copy-pack step is needed.
USTRUCT()
struct FTempoLidarTile
{
	GENERATED_BODY()

	// Rotation relative to the parent lidar (yaw only).
	UPROPERTY(VisibleAnywhere)
	float YawOffset = 0.0f;

	// This tile's horizontal FOV in degrees.
	UPROPERTY(VisibleAnywhere)
	float FOVAngle = 120.0f;

	// Number of horizontal beams this tile covers.
	UPROPERTY(VisibleAnywhere)
	int32 HorizontalBeams = 0;

	// Render size in pixels (ceil of SizeXYFOV).
	UPROPERTY(VisibleAnywhere)
	FIntPoint SizeXY = FIntPoint::ZeroValue;

	// Image plane size encompassing the lidar FOV in (upsampled) pixels. SizeXY is the ceil of this.
	UPROPERTY(VisibleAnywhere)
	FVector2D SizeXYFOV = FVector2D::ZeroVector;

	// Distortion factor introduced by the spherical-to-perspective mapping.
	UPROPERTY(VisibleAnywhere)
	double DistortionFactor = 1.0;

	// Effective vertical FOV after the distortion factor is applied.
	UPROPERTY(VisibleAnywhere)
	double DistortedVerticalFOV = 30.0;

	// Horizontal offset of this tile inside SharedTextureTarget (tiles pack L→C→R).
	UPROPERTY(VisibleAnywhere)
	int32 SliceDestOffsetX = 0;

	UPROPERTY(VisibleAnywhere)
	float MinDepth = 10.0f;

	UPROPERTY(VisibleAnywhere)
	float MaxDepth = 40000.0f;

	UPROPERTY(VisibleAnywhere)
	bool bActive = false;

	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance = nullptr;

	UPROPERTY()
	FPostProcessSettings PostProcessSettings;

	// Not a UPROPERTY — manages its own lifetime.
	FSceneViewStateReference ViewState;

	// One-shot camera-cut flag consumed by the next multi-view render.
	bool bCameraCut = false;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOLIDAR_API UTempoLidar : public UTempoTiledSceneCaptureComponent
{
	GENERATED_BODY()

public:
	UTempoLidar();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// Begin ITempoSensorInterface
	virtual TOptional<TFuture<void>> SendMeasurements() override;
	// End ITempoSensorInterface

	void RequestMeasurement(const TempoLidar::LidarScanRequest& Request, const TResponseDelegate<TempoLidar::LidarScanSegment>& ResponseContinuation);

protected:
	// UTempoSceneCaptureComponent2D hooks: the lidar manages its own single readback from the packed
	// SharedTextureTarget and drives its own timer; it never calls CaptureScene() on itself.
	virtual bool HasPendingRequests() const override { return !PendingRequests.IsEmpty(); }
	virtual void InitRenderTarget() override;
	virtual void MaybeCapture() override;

	TFuture<void> DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads);

	// Initialize the shared packed render target and ring of staging textures. Also assigns each
	// active tile its SliceDestOffsetX and the packed dimensions.
	void InitSharedRenderTarget();

	// Begin UTempoTiledSceneCaptureComponent tile interface
	virtual void InitTileSlots() override;
	virtual void SyncTiles() override;
	virtual bool HasDetectedParameterChange() const override;
	virtual void ReconfigureTilesNow() override;
	virtual void UpdateInternalMirrors() override;
	// End UTempoTiledSceneCaptureComponent tile interface

	void ConfigureTile(FTempoLidarTile& Tile, double InYawOffset, double SubHorizontalFOV, int32 SubHorizontalBeams, bool bActivate);
	void ApplyTilePostProcess(FTempoLidarTile& Tile);
	void AllocateTileViewState(FTempoLidarTile& Tile);
	void DeactivateTile(FTempoLidarTile& Tile);

	// Returns the vertical FOV to use for tile sizing: derived from BeamCalibration when set,
	// otherwise falls back to the VerticalFOV property.
	double GetEffectiveVerticalFOV() const;

	// Returns the vertical beam count: BeamCalibration.Num() when set, else VerticalBeams.
	int32 GetEffectiveVerticalBeams() const;

	// The measurement types supported. Should be set in constructor of derived classes.
	// (MeasurementTypes is inherited from UTempoTiledSceneCaptureComponent.)

	// The minimum distance this Lidar can measure. Note that GEngine->NearClipPlane must be less than this value.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	double MinDistance = 10.0; // 10cm

	// The maximum distance this Lidar can measure. Note that UTempoSensorsSettings::MaxLidarDepth must be greater than this value.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	double MaxDistance = 10000.0; // 100m

	// The distance closer than which perpendicular surfaces will have saturated (1.0) intensity.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	double IntensitySaturationDistance = 1000.0; // 10m

	// The vertical field of view in degrees.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0001, UIMax=180.0, ClampMin=0.0001, ClampMax=180.0))
	double VerticalFOV = 30.0;

	// The horizontal field of view in degrees.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0001, UIMax=360.0, ClampMin=0.0001, ClampMax=360.0))
	double HorizontalFOV = 120.0;

	// The number of vertical beams.
	UPROPERTY(EditAnywhere, meta=(UIMin=1, ClampMin=1))
	int32 VerticalBeams = 64;

	// The number of horizontal beams.
	UPROPERTY(EditAnywhere, meta=(UIMin=0, ClampMin=0))
	int32 HorizontalBeams = 200;

	// The max angle of incidence in degrees from which this Lidar will get a return.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, UIMax=90.0, ClampMin=0.0, ClampMax=90.0))
	float MaxAngleOfIncidence = 87.5;

	// Per-beam intrinsic calibration. When non-empty, replaces the uniform VerticalBeams /
	// VerticalFOV grid. Array length determines the beam count; VerticalFOV is derived
	// automatically as a symmetric FOV that encloses all configured elevation angles.
	// Entries are in channel index order (need not be sorted by elevation).
	UPROPERTY(EditAnywhere)
	TArray<FLidarBeamCalibration> BeamCalibration;

	// RateHz and SequenceId are inherited from UTempoSceneCaptureComponent2D.

	TArray<FLidarScanRequest> PendingRequests;

	// Tile slots (fixed size: L=0, C=1, R=2). Stable storage — indices are never invalidated
	// and held addresses remain valid across reconfigures. Transient: runtime-only state.
	UPROPERTY(Transient)
	FTempoLidarTile Tiles[3];

	// Mirrors of the watched properties. Updated in ReconfigureTilesNow.
	double HorizontalFOV_Internal = -1.0;
	double VerticalFOV_Internal = -1.0;
	int32 HorizontalBeams_Internal = -1;
	int32 VerticalBeams_Internal = -1;
	TArray<FLidarBeamCalibration> BeamCalibration_Internal;

	friend struct TTextureRead<FLidarPixel>;
};

template <>
struct TTextureRead<FLidarPixel> : TTextureReadBase<FLidarPixel>
{
	TTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
		const FString& SensorNameIn, const FTransform& SensorTransformIn, const FTransform& CaptureTransform,
		double HorizontalFOVIn, double VerticalFOVIn, int32 HorizontalBeamsIn, int32 VerticalBeamsIn, const FVector2D& SizeXYFOVIn,
		double IntensitySaturationDistanceIn, double MaxAngleOfIncidenceIn,
		int32 NumCaptureComponentsIn, double RelativeYawIn, float MinDepthIn, float MaxDepthIn, double MinDistanceIn, double MaxDistanceIn,
		TArray<FLidarBeamCalibration> BeamCalibrationIn)
		: TTextureReadBase(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn, SensorTransformIn),
			CaptureTransform(CaptureTransform), HorizontalFOV(HorizontalFOVIn), VerticalFOV(VerticalFOVIn), HorizontalBeams(HorizontalBeamsIn),
			VerticalBeams(VerticalBeamsIn), SizeXYFOV(SizeXYFOVIn), IntensitySaturationDistance(IntensitySaturationDistanceIn),
			MaxAngleOfIncidence(MaxAngleOfIncidenceIn), NumCaptureComponents(NumCaptureComponentsIn), RelativeYaw(RelativeYawIn),
			MinDepth(MinDepthIn), MaxDepth(MaxDepthIn), MinDistance(MinDistanceIn), MaxDistance(MaxDistanceIn),
			BeamCalibration(MoveTemp(BeamCalibrationIn))
	{
	}

	virtual FName GetType() const override { return TEXT("Lidar"); }

	void Decode(float TransmissionTime, TempoLidar::LidarScanSegment& ScanSegmentOut) const;

	const FTransform CaptureTransform;
	double HorizontalFOV;
	double VerticalFOV;
	int32 HorizontalBeams;
	int32 VerticalBeams;
	const FVector2D SizeXYFOV;
	double IntensitySaturationDistance;
	double MaxAngleOfIncidence;
	int32 NumCaptureComponents;
	double RelativeYaw;
	float MinDepth;
	float MaxDepth;
	double MinDistance;
	double MaxDistance;
	TArray<FLidarBeamCalibration> BeamCalibration;
};

// A texture read that holds the horizontally-packed pixels of all active lidar slices.
// After readback, SplitIntoSlices() produces one TTextureRead<FLidarPixel> per slice so that
// the existing parallel Decode path (which expects per-slice reads) can proceed unchanged.
struct FLidarSharedTextureRead : TTextureReadBase<FLidarPixel>
{
	FLidarSharedTextureRead(const FIntPoint& PackedSizeIn, int32 SequenceIdIn, double CaptureTimeIn,
		const FString& OwnerNameIn, const FString& SensorNameIn, const FTransform& SensorTransformIn,
		TArray<TUniquePtr<TTextureRead<FLidarPixel>>> SlicesIn)
		: TTextureReadBase(PackedSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn, SensorTransformIn),
		  Slices(MoveTemp(SlicesIn))
	{
	}

	virtual FName GetType() const override { return TEXT("LidarShared"); }

	// Move per-slice pixels out of the packed Image into each slice's own Image, returning ownership
	// of the per-slice reads to the caller. This instance should not be used afterward.
	TArray<TUniquePtr<FTextureRead>> SplitIntoSlices();

	// Per-slice reads preallocated at capture time with the correct metadata and sized buffers.
	TArray<TUniquePtr<TTextureRead<FLidarPixel>>> Slices;
};
