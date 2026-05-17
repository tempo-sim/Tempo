// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoTiledSceneCaptureComponent.h"

#include "TempoSensors/Lidar.pb.h"

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "SceneTypes.h"
#include "TempoServer.h"
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

// 16-byte pixel format used when color rendering is enabled. PF_A32B32G32R32F atlas: 4 float32
// lanes per pixel, each carrying 24 bits of payload in its low bits. The WithColor PPM packs each
// lane as `(payload & 0x00FFFFFF) | 0x3F000000`, which forces the float into [0.5, 2.0) — always a
// normal positive float, so the GPU output stage can't flush it to zero (denormal) or canonicalize
// it to a fixed pattern (NaN). The high 8 bits are a fixed safety prefix and are masked off here.
// (PF_R32G32B32A32_UINT would have skipped this dance but isn't in IsSupportedFormat's whitelist.)
//   Lane 0 (channel R, low 24 bits, low->high bytes): B, G, R          — packed 24-bit BGR color
//   Lane 1 (channel G, low 24 bits, low->high bytes): Nx, Ny, Nz       — packed octet normal
//   Lane 2 (channel B, low 24 bits): discretized inverse depth         — max 2^24 levels
//   Lane 3 (channel A, low 24 bits, low->high bytes): Label, spare, spare
struct FLidarPixelWithColor
{
	static constexpr bool bSupportsDepth = false;

	uint8 B() const { return static_cast<uint8>(Payload(0) & 0xFFu); }
	uint8 G() const { return static_cast<uint8>((Payload(0) >> 8) & 0xFFu); }
	uint8 R() const { return static_cast<uint8>((Payload(0) >> 16) & 0xFFu); }
	uint8 Label() const { return static_cast<uint8>(Payload(3) & 0xFFu); }

	FVector Normal() const
	{
		const uint8 Nx = static_cast<uint8>(Payload(1) & 0xFFu);
		const uint8 Ny = static_cast<uint8>((Payload(1) >> 8) & 0xFFu);
		const uint8 Nz = static_cast<uint8>((Payload(1) >> 16) & 0xFFu);
		return FVector(2.0 * Nx / 255.0 - 1.0, 2.0 * Ny / 255.0 - 1.0, 2.0 * Nz / 255.0 - 1.0);
	}

	float Depth(float MinDepth, float MaxDepth, float MaxDiscretizedDepth) const
	{
		const float InverseDepthFraction = static_cast<float>(Payload(2)) / MaxDiscretizedDepth;
		const float InverseDepth = InverseDepthFraction * (1.0 / MinDepth - 1.0 / MaxDepth) + 1.0 / MaxDepth;
		return 1.0 / InverseDepth;
	}

private:
	// Strip the 0x3F000000 safety prefix the PPM ORs in. The low 24 bits are the actual payload.
	uint32 Payload(int Index) const { return Lanes[Index] & 0x00FFFFFFu; }

	uint32 Lanes[4] = {0u, 0u, 0u, 0u};
};
static_assert(sizeof(FLidarPixelWithColor) == 16, "FLidarPixelWithColor must match PF_R32G32B32A32_UINT stride");

struct FLidarScanRequest
{
	TempoSensors::LidarScanRequest Request;
	TResponseDelegate<TempoSensors::LidarScanSegment> ResponseContinuation;
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
// UTempoLidar::RenderCapture via TempoMultiViewCapture::RenderTiles, which writes each view directly
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
class TEMPOSENSORS_API UTempoLidar : public UTempoTiledSceneCaptureComponent
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

	void RequestMeasurement(const TempoSensors::LidarScanRequest& Request, const TResponseDelegate<TempoSensors::LidarScanSegment>& ResponseContinuation);

	// USceneCaptureComponent::AddReferencedObjects only walks the inherited ViewStates indirect
	// array; our per-tile FSceneViewStateReference members aren't in that array, so the MIDPool
	// each view state holds (UMaterialInstanceDynamic instances created on demand by
	// UMaterialInterface::OverrideBlendableSettings for any PostProcessVolume material applied to
	// the view) is unreachable from GC. Once those MIDs get collected, queued render commands
	// still hold raw pointers to their FMaterialRenderProxy and fatal in CacheUniformExpressions.
	// Forward each tile's view state to the collector to keep its MIDPool reachable.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

protected:
	// UTempoSceneCaptureComponent2D hooks: the lidar manages its own single readback from the packed
	// SharedTextureTarget and drives its own timer; it never calls CaptureScene() on itself.
	virtual bool HasPendingRequests() const override { return !PendingRequests.IsEmpty(); }
	virtual int32 GetNumActiveTiles() const override;
	virtual void InitRenderTarget() override;
	virtual void RenderCapture() override;

	TFuture<void> DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads, bool bWithColor);

	// Initialize the shared packed render target and ring of staging textures. Also assigns each
	// active tile its SliceDestOffsetX and the packed dimensions. Format depends on bColorEnabled.
	void InitSharedRenderTarget();

	// Begin UTempoTiledSceneCaptureComponent tile interface
	virtual void SyncTiles() override;
	virtual bool HasDetectedParameterChange() const override;
	virtual void ReconfigureTilesNow() override;
	virtual void UpdateInternalMirrors() override;
	// End UTempoTiledSceneCaptureComponent tile interface

	void ConfigureTile(FTempoLidarTile& Tile, double InYawOffset, double SubHorizontalFOV, int32 SubHorizontalBeams, bool bActivate);
	void ApplyTilePostProcess(FTempoLidarTile& Tile);
	void AllocateTileViewState(FTempoLidarTile& Tile);
	void DeactivateTile(FTempoLidarTile& Tile);

	// Mirrors TempoCamera's SetDepthEnabled/ApplyDepthEnabled: SetColorEnabled is a no-op if the
	// flag is unchanged, otherwise calls ApplyColorEnabled. ApplyColorEnabled drains in-flight
	// reads, swaps each active tile's PPM to the matching variant, applies the photoreal vs
	// no-color render settings + ray-tracing toggle on the family-level container (this component),
	// and reinits the shared atlas + staging ring at the new format.
	void SetColorEnabled(bool bColorEnabledIn);
	void ApplyColorEnabled();

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
	UPROPERTY(EditAnywhere, meta=(UIMin=2, ClampMin=2))
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

	// Whether this lidar is currently rendering color. Driven automatically: flips on when any
	// pending request sets include_color, flips off when none do. Same drain-then-reinit flow as
	// TempoCamera's bDepthEnabled. The toggle has a real GPU cost (color mode enables Lumen +
	// ray tracing); kept off-by-default so plain scan requests pay nothing extra.
	UPROPERTY(VisibleAnywhere, Category="Tempo")
	bool bColorEnabled = false;

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

// Shared metadata + ctor for both the no-color and with-color per-slice reads. The pixel type
// is the only difference between the two specializations.
template <typename PixelType>
struct TLidarTextureReadBase : TTextureReadBase<PixelType>
{
	TLidarTextureReadBase(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
		const FString& SensorNameIn, const FTransform& SensorTransformIn, const FTransform& CaptureTransformIn,
		double HorizontalFOVIn, double VerticalFOVIn, int32 HorizontalBeamsIn, int32 VerticalBeamsIn, const FVector2D& SizeXYFOVIn,
		double IntensitySaturationDistanceIn, double MaxAngleOfIncidenceIn,
		int32 NumCaptureComponentsIn, double RelativeYawIn, float MinDepthIn, float MaxDepthIn, double MinDistanceIn, double MaxDistanceIn,
		TArray<FLidarBeamCalibration> BeamCalibrationIn)
		: TTextureReadBase<PixelType>(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn, SensorTransformIn),
			CaptureTransform(CaptureTransformIn), HorizontalFOV(HorizontalFOVIn), VerticalFOV(VerticalFOVIn), HorizontalBeams(HorizontalBeamsIn),
			VerticalBeams(VerticalBeamsIn), SizeXYFOV(SizeXYFOVIn), IntensitySaturationDistance(IntensitySaturationDistanceIn),
			MaxAngleOfIncidence(MaxAngleOfIncidenceIn), NumCaptureComponents(NumCaptureComponentsIn), RelativeYaw(RelativeYawIn),
			MinDepth(MinDepthIn), MaxDepth(MaxDepthIn), MinDistance(MinDistanceIn), MaxDistance(MaxDistanceIn),
			BeamCalibration(MoveTemp(BeamCalibrationIn))
	{
	}

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

template <>
struct TTextureRead<FLidarPixel> : TLidarTextureReadBase<FLidarPixel>
{
	using TLidarTextureReadBase::TLidarTextureReadBase;

	virtual FName GetType() const override { return TEXT("Lidar"); }

	void Decode(float TransmissionTime, TempoSensors::LidarScanSegment& ScanSegmentOut) const;
};

template <>
struct TTextureRead<FLidarPixelWithColor> : TLidarTextureReadBase<FLidarPixelWithColor>
{
	using TLidarTextureReadBase::TLidarTextureReadBase;

	virtual FName GetType() const override { return TEXT("LidarColor"); }

	void Decode(float TransmissionTime, TempoSensors::LidarScanSegment& ScanSegmentOut) const;
};

// A texture read that holds the horizontally-packed pixels of all active lidar slices.
// After readback, SplitIntoSlices() produces one TTextureRead<PixelType> per slice so that
// the existing parallel Decode path (which expects per-slice reads) can proceed unchanged.
template <typename PixelType>
struct TLidarSharedTextureRead : TTextureReadBase<PixelType>
{
	TLidarSharedTextureRead(const FIntPoint& PackedSizeIn, int32 SequenceIdIn, double CaptureTimeIn,
		const FString& OwnerNameIn, const FString& SensorNameIn, const FTransform& SensorTransformIn,
		TArray<TUniquePtr<TTextureRead<PixelType>>> SlicesIn)
		: TTextureReadBase<PixelType>(PackedSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn, SensorTransformIn),
		  Slices(MoveTemp(SlicesIn))
	{
	}

	virtual FName GetType() const override;

	// Move per-slice pixels out of the packed Image into each slice's own Image, returning ownership
	// of the per-slice reads to the caller. This instance should not be used afterward.
	TArray<TUniquePtr<FTextureRead>> SplitIntoSlices();

	// Per-slice reads preallocated at capture time with the correct metadata and sized buffers.
	TArray<TUniquePtr<TTextureRead<PixelType>>> Slices;
};
