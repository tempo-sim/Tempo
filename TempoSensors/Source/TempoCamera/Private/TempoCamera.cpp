// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"

#include "TempoActorLabeler.h"
#include "TempoCameraModule.h"
#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"
#include "TempoLabelTypes.h"
#include "TempoSensorsConstants.h"
#include "TempoMultiViewCapture.h"
#include "TempoSensorsSettings.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Box2D.h"
#include "Math/PerspectiveMatrix.h"
#include "TextureResource.h"

namespace
{
	constexpr double MaxPerspectiveFOVPerCapture = 120.0;
}

/**
 * Compute 2D bounding boxes from label data.
 */
static TMap<int32, FBox2D> ComputeBoundingBoxes(const TArray<uint8>& LabelData, uint32 Width, uint32 Height)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ComputeBoundingBoxes);

	TMap<int32, FBox2D> Boxes;
	for (uint32 Y = 0; Y < Height; ++Y)
	{
		for (uint32 X = 0; X < Width; ++X)
		{
			const uint8 InstanceId = LabelData[Y * Width + X];
			if (InstanceId > 0)
			{
				Boxes.FindOrAdd(InstanceId) += FUintPoint(X, Y);
			}
		}
	}
	return Boxes;
}

FTempoCameraIntrinsics::FTempoCameraIntrinsics(const FIntPoint& SizeXY, float HorizontalFOV)
	: Fx(SizeXY.X / 2.0 / FMath::Tan(FMath::DegreesToRadians(HorizontalFOV) / 2.0)),
	  Fy(Fx),
	  Cx(SizeXY.X / 2.0),
	  Cy(SizeXY.Y / 2.0) {}

template <typename PixelType>
void ExtractPixelData(const PixelType& Pixel, EColorImageEncoding Encoding, char* Dest)
{
	switch (Encoding)
	{
		case EColorImageEncoding::BGR8:
		{
			Dest[0] = Pixel.B();
			Dest[1] = Pixel.G();
			Dest[2] = Pixel.R();
			break;
		}
		case EColorImageEncoding::RGB8:
		{
			Dest[0] = Pixel.R();
			Dest[1] = Pixel.G();
			Dest[2] = Pixel.B();
			break;
		}
	}
}

TempoCamera::ColorEncoding ColorEncodingToProto(EColorImageEncoding Encoding)
{
	switch (Encoding)
	{
		case EColorImageEncoding::BGR8:
		{
			return TempoCamera::ColorEncoding::BGR8;
		}
		case EColorImageEncoding::RGB8:
		default:
		{
			return TempoCamera::ColorEncoding::RGB8;
		}
	}
}

template <typename PixelType>
void RespondToColorRequests(const TTextureRead<PixelType>* TextureRead, const TArray<FColorImageRequest>& Requests, float TransmissionTime)
{
	TempoCamera::ColorImage ColorImage;
	if (!Requests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeColor);
		ColorImage.set_width(TextureRead->ImageSize.X);
		ColorImage.set_height(TextureRead->ImageSize.Y);

		std::vector<char> ImageData;
		ImageData.resize(TextureRead->Image.Num() * 3);

		const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
		if (!TempoSensorsSettings)
		{
			return;
		}

		const EColorImageEncoding Encoding = TempoSensorsSettings->GetColorImageEncoding();
		ParallelFor(TextureRead->Image.Num(), [&ImageData, &TextureRead, Encoding](int32 Idx)
		{
			ExtractPixelData(TextureRead->Image[Idx], Encoding, &ImageData[Idx * 3]);
		});

		ColorImage.mutable_data()->assign(ImageData.begin(), ImageData.end());
		ColorImage.set_encoding(ColorEncodingToProto(Encoding));
		TextureRead->ExtractMeasurementHeader(TransmissionTime, ColorImage.mutable_header());
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondColor);
	for (auto ColorImageRequestIt = Requests.CreateConstIterator(); ColorImageRequestIt; ++ColorImageRequestIt)
	{
		ColorImageRequestIt->ResponseContinuation.ExecuteIfBound(ColorImage, grpc::Status_OK);
	}
}

template <typename PixelType>
void RespondToLabelRequests(const TTextureRead<PixelType>* TextureRead, const TArray<FLabelImageRequest>& Requests, float TransmissionTime)
{
	TempoCamera::LabelImage LabelImage;
	if (!Requests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeLabel);
		LabelImage.set_width(TextureRead->ImageSize.X);
		LabelImage.set_height(TextureRead->ImageSize.Y);

		std::vector<char> ImageData;
		ImageData.resize(TextureRead->Image.Num());

		ParallelFor(TextureRead->Image.Num(), [&ImageData, &TextureRead](int32 Idx)
		{
			ImageData[Idx] = TextureRead->Image[Idx].Label();
		});

		LabelImage.mutable_data()->assign(ImageData.begin(), ImageData.end());
		TextureRead->ExtractMeasurementHeader(TransmissionTime, LabelImage.mutable_header());
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondLabel);
	for (auto LabelImageRequestIt = Requests.CreateConstIterator(); LabelImageRequestIt; ++LabelImageRequestIt)
	{
		LabelImageRequestIt->ResponseContinuation.ExecuteIfBound(LabelImage, grpc::Status_OK);
	}
}

template <typename PixelType>
void RespondToBoundingBoxRequests(const TTextureRead<PixelType>* TextureRead, const TArray<FBoundingBoxesRequest>& Requests, float TransmissionTime)
{
	TempoCamera::BoundingBoxes Response;
	if (!Requests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeBoundingBoxes);

		Response.set_width(TextureRead->ImageSize.X);
		Response.set_height(TextureRead->ImageSize.Y);
		TextureRead->ExtractMeasurementHeader(TransmissionTime, Response.mutable_header());

		TArray<uint8> LabelData;
		LabelData.SetNumUninitialized(TextureRead->Image.Num());
		ParallelFor(TextureRead->Image.Num(), [&LabelData, &TextureRead](int32 Idx)
		{
			LabelData[Idx] = TextureRead->Image[Idx].Label();
		});

		TMap<int32, FBox2D> BoundingBoxes = ComputeBoundingBoxes(LabelData, TextureRead->ImageSize.X, TextureRead->ImageSize.Y);

		for (const auto& [InstanceId, Box] : BoundingBoxes)
		{
			TempoCamera::BoundingBox2D* BBoxProto = Response.add_bounding_boxes();
			BBoxProto->set_min_x(FMath::RoundToInt32(Box.Min.X));
			BBoxProto->set_min_y(FMath::RoundToInt32(Box.Min.Y));
			BBoxProto->set_max_x(FMath::RoundToInt32(Box.Max.X));
			BBoxProto->set_max_y(FMath::RoundToInt32(Box.Max.Y));
			BBoxProto->set_instance_id(InstanceId);

			const uint8* SemanticId = TextureRead->InstanceToSemanticMap.Find(InstanceId);
			if (!SemanticId)
			{
				UE_LOG(LogTempoCamera, Warning, TEXT("No semantic ID found for instance ID %d"), InstanceId);
			}
			BBoxProto->set_semantic_id(SemanticId ? *SemanticId : 0);
		}
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondBoundingBoxes);
	for (auto RequestIt = Requests.CreateConstIterator(); RequestIt; ++RequestIt)
	{
		RequestIt->ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
	}
}

void TTextureRead<FCameraPixelNoDepth>::RespondToRequests(const TArray<FColorImageRequest>& Requests, float TransmissionTime) const
{
	RespondToColorRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelNoDepth>::RespondToRequests(const TArray<FLabelImageRequest>& Requests, float TransmissionTime) const
{
	RespondToLabelRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelNoDepth>::RespondToRequests(const TArray<FBoundingBoxesRequest>& Requests, float TransmissionTime) const
{
	RespondToBoundingBoxRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FColorImageRequest>& Requests, float TransmissionTime) const
{
	RespondToColorRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FLabelImageRequest>& Requests, float TransmissionTime) const
{
	RespondToLabelRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FDepthImageRequest>& Requests, float TransmissionTime) const
{
	TempoCamera::DepthImage DepthImage;
	if (!Requests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeDepth);
		DepthImage.set_width(ImageSize.X);
		DepthImage.set_height(ImageSize.Y);
		DepthImage.mutable_depths()->Resize(ImageSize.X * ImageSize.Y, 0.0);

		ParallelFor(Image.Num(), [&DepthImage, this](int32 Idx)
		{
			DepthImage.set_depths(Idx, Image[Idx].Depth(MinDepth, MaxDepth, GTempoCamera_Max_Discrete_Depth));
		});

		ExtractMeasurementHeader(TransmissionTime, DepthImage.mutable_header());
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondDepth);
	for (auto DepthImageRequestIt = Requests.CreateConstIterator(); DepthImageRequestIt; ++DepthImageRequestIt)
	{
		DepthImageRequestIt->ResponseContinuation.ExecuteIfBound(DepthImage, grpc::Status_OK);
	}
}

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FBoundingBoxesRequest>& Requests, float TransmissionTime) const
{
	RespondToBoundingBoxRequests(this, Requests, TransmissionTime);
}

// ------------------------------------------------------------------------------------
// UTempoCamera
// ------------------------------------------------------------------------------------

UTempoCamera::UTempoCamera()
{
	PrimaryComponentTick.bCanEverTick = true;
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE, EMeasurementType::BOUNDING_BOXES };
	bAutoActivate = true;

	// UTempoCamera itself doubles as the proxy scene capture: its PPM replaces scene color with
	// the stitched HDR texture, then bloom/AE/tonemapper produce LDR for the merge pass to pack
	// together with label+depth bytes.
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	PixelFormatOverride = EPixelFormat::PF_Unknown;

	// Auto exposure for the proxy's tonemap pass. AEM_Histogram samples the (PPM-replaced) scene
	// color histogram to compute exposure.
	PostProcessSettings.bOverride_AutoExposureMethod = true;
	PostProcessSettings.AutoExposureMethod = AEM_Histogram;
	PostProcessSettings.bOverride_AutoExposureBias = true;
	PostProcessSettings.AutoExposureBias = 1.0;
	PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = 0;
	PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
	PostProcessSettings.AutoExposureSpeedUp = 20.0;
	PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
	PostProcessSettings.AutoExposureSpeedDown = 20.0;
	PostProcessSettings.bOverride_AutoExposureLowPercent = true;
	PostProcessSettings.AutoExposureLowPercent = 75.0;
	PostProcessSettings.bOverride_AutoExposureHighPercent = true;
	PostProcessSettings.AutoExposureHighPercent = 85.0;
	PostProcessSettings.bOverride_MotionBlurAmount = true;
	PostProcessSettings.MotionBlurAmount = 0.0;

	// Reasonable defaults
	ShowFlags.SetMotionBlur(true);
	ShowFlags.SetAntiAliasing(true);
	ShowFlags.SetTemporalAA(true);
	ShowFlags.SetEyeAdaptation(true);
	ShowFlags.SetLocalExposure(true);
	ShowFlags.SetLensFlares(true);
	ShowFlags.SetBloom(true);
	ShowFlags.SetColorGrading(true);
	ShowFlags.SetVignette(true);
	ShowFlags.SetDepthOfField(true);
}

void UTempoCamera::OnRegister()
{
	Super::OnRegister();

	// Don't configure tiles during cooking or for template/archetype objects (e.g. Blueprint
	// editor previews where GetOwner() is not a properly-packaged actor).
	if (IsRunningCommandlet() || IsTemplate())
	{
		return;
	}

	// Fixed four tile slots (TL=0, TR=1, BL=2, BR=3). Pre-allocate so tile addresses are stable
	// across SyncTiles calls and no TArray reallocation ever runs.
	if (Tiles.Num() != 4)
	{
		Tiles.SetNum(4);
	}

	SyncTiles();
	UpdateInternalMirrors();

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		// Activate() runs when added to a live world, but may be skipped in some registration
		// paths. Init render targets here so they are ready for the first capture regardless.
		InitRenderTarget();
	}
}

void UTempoCamera::BeginPlay()
{
	Super::BeginPlay();
}

void UTempoCamera::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (HasDetectedParameterChange())
	{
		bReconfigurePending = true;
	}
	TryApplyPendingReconfigure();
}

bool UTempoCamera::HasDetectedParameterChange() const
{
	return SizeXY != SizeXY_Internal
		|| FOVAngle != FOVAngle_Internal
		|| LensParameters != LensParameters_Internal;
}

void UTempoCamera::TryApplyPendingReconfigure()
{
	// Don't reconfigure during cooking or for template/archetype objects.
	if (IsRunningCommandlet() || IsTemplate())
	{
		return;
	}
	if (!bReconfigurePending)
	{
		return;
	}
	// Only reconfigure when no readback is in flight — tiles feed a single shared queue on the
	// owner, so we can gate on its length.
	if (TextureReadQueue.Num() > 0)
	{
		return;
	}
	ReconfigureTilesNow();
	bReconfigurePending = false;
}

void UTempoCamera::ReconfigureTilesNow()
{
	// Drain all active tiles (releases their view states + PPMs) before re-configuring with
	// new parameters. Caller must have confirmed no reads are in flight.
	for (FTempoCameraTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			DeactivateTile(Tile);
		}
	}

	SyncTiles();
	UpdateInternalMirrors();

	// Render targets need to resize when SizeXY changed, and any queued reads reference the
	// previous RT geometry so are no longer valid.
	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitRenderTarget();
	}
}

void UTempoCamera::UpdateInternalMirrors()
{
	LensParameters_Internal = LensParameters;
	FOVAngle_Internal = FOVAngle;
	SizeXY_Internal = SizeXY;
}

FString UTempoCamera::GetOwnerName() const
{
	check(GetOwner());
	return GetOwner()->GetActorNameOrLabel();
}

FString UTempoCamera::GetSensorName() const
{
	return GetName();
}

bool UTempoCamera::HasPendingCameraRequests() const
{
	return !PendingColorImageRequests.IsEmpty() || !PendingLabelImageRequests.IsEmpty() || !PendingDepthImageRequests.IsEmpty() || !PendingBoundingBoxesRequests.IsEmpty();
}

bool UTempoCamera::IsAwaitingRender()
{
	return TextureReadQueue.IsNextAwaitingRender();
}

void UTempoCamera::OnRenderCompleted()
{
	if (!TextureReadQueue.IsNextAwaitingRender() || !SharedFinalTextureTarget)
	{
		return;
	}

	const FRenderTarget* RenderTarget = SharedFinalTextureTarget->GetRenderTargetResource();
	if (!ensureMsgf(RenderTarget, TEXT("SharedFinalTextureTarget was not initialized. Skipping texture read.")))
	{
		return;
	}

	const bool bShouldBlock = GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep
		&& !GetDefault<UTempoSensorsSettings>()->GetPipelinedRendering();

	TextureReadQueue.ReadAllAvailable(RenderTarget, bShouldBlock);
}

void UTempoCamera::BlockUntilMeasurementsReady() const
{
	TextureReadQueue.BlockUntilNextReadComplete();
}

TOptional<TFuture<void>> UTempoCamera::SendMeasurements()
{
	TOptional<TFuture<void>> Future;

	// Snapshot depth-request state before draining, so the toggle decision below sees ongoing
	// client intent in the steady state (one depth request per frame, drained the same frame)
	// instead of oscillating off/on when the queue looks empty post-drain.
	const bool bHadDepthRequests = !PendingDepthImageRequests.IsEmpty();

	if (TextureReadQueue.NextReadComplete())
	{
		TUniquePtr<FTextureRead> TextureRead = TextureReadQueue.DequeueIfReadComplete();
		const bool bReadHasDepth = TextureRead->GetType() == TEXT("WithDepth");
		Future = DecodeAndRespond(MoveTemp(TextureRead));

		PendingColorImageRequests.Empty();
		PendingLabelImageRequests.Empty();
		PendingBoundingBoxesRequests.Empty();
		// A NoDepth read that completes while bDepthEnabled has since flipped to true (or vice
		// versa) cannot satisfy depth requests. Key off the read's type rather than the current
		// bDepthEnabled so stale requests aren't dropped across a toggle boundary.
		if (bReadHasDepth)
		{
			PendingDepthImageRequests.Empty();
		}
	}

	// Toggle depth based on client intent. Use the pre-drain snapshot OR'd with any requests
	// still pending (either newly arrived during this function, or left over because the last
	// read couldn't satisfy them).
	const bool bDepthNeeded = bHadDepthRequests || !PendingDepthImageRequests.IsEmpty();
	if (!bDepthEnabled && bDepthNeeded)
	{
		SetDepthEnabled(true);
	}
	if (bDepthEnabled && !bDepthNeeded)
	{
		SetDepthEnabled(false);
	}

	// Take the opportunity to apply any reconfigure that was detected earlier but had to be
	// deferred because reads were still in flight.
	TryApplyPendingReconfigure();

	return Future;
}

void UTempoCamera::RequestMeasurement(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation)
{
	PendingColorImageRequests.Add({ Request, ResponseContinuation});
}

void UTempoCamera::RequestMeasurement(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation)
{
	PendingLabelImageRequests.Add({ Request, ResponseContinuation});
}

void UTempoCamera::RequestMeasurement(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation)
{
	PendingDepthImageRequests.Add({ Request, ResponseContinuation});
}

void UTempoCamera::RequestMeasurement(const TempoCamera::BoundingBoxesRequest& Request, const TResponseDelegate<TempoCamera::BoundingBoxes>& ResponseContinuation)
{
	PendingBoundingBoxesRequests.Add({ Request, ResponseContinuation});
}

FTempoCameraIntrinsics UTempoCamera::GetIntrinsics() const
{
	return FTempoCameraIntrinsics(SizeXY, FOVAngle);
}

TFuture<void> UTempoCamera::DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		TextureRead = MoveTemp(TextureRead),
		ColorImageRequests = PendingColorImageRequests,
		LabelImageRequests = PendingLabelImageRequests,
		DepthImageRequests = PendingDepthImageRequests,
		BoundingBoxRequests = PendingBoundingBoxesRequests,
		TransmissionTimeCpy = TransmissionTime
	]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeAndRespond);

		if (TextureRead->GetType() == TEXT("WithDepth"))
		{
			static_cast<TTextureRead<FCameraPixelWithDepth>*>(TextureRead.Get())->RespondToRequests(ColorImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelWithDepth>*>(TextureRead.Get())->RespondToRequests(LabelImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelWithDepth>*>(TextureRead.Get())->RespondToRequests(DepthImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelWithDepth>*>(TextureRead.Get())->RespondToRequests(BoundingBoxRequests, TransmissionTimeCpy);
		}
		else if (TextureRead->GetType() == TEXT("NoDepth"))
		{
			static_cast<TTextureRead<FCameraPixelNoDepth>*>(TextureRead.Get())->RespondToRequests(ColorImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelNoDepth>*>(TextureRead.Get())->RespondToRequests(LabelImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelNoDepth>*>(TextureRead.Get())->RespondToRequests(BoundingBoxRequests, TransmissionTimeCpy);
		}
	});

	return Future;
}

// ------------------------------------------------------------------------------------
// Shared RT / capture timer
// ------------------------------------------------------------------------------------

void UTempoCamera::Activate(bool bReset)
{
	// Super::Activate calls InitRenderTarget() (our override) when in a game world. It does not
	// start its own capture timer because ShouldManageOwnTimer() returns false.
	Super::Activate(bReset);

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		RestartCaptureTimer();
	}
}

void UTempoCamera::Deactivate()
{
	Super::Deactivate();

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TimerHandle);
		}
		TextureReadQueue.Empty();
	}
}

static float GetCameraTimerPeriod(float RateHz)
{
	return 1.0 / FMath::Max(UE_KINDA_SMALL_NUMBER, RateHz);
}

void UTempoCamera::RestartCaptureTimer()
{
	if (UWorld* World = GetWorld())
	{
		const float TimerPeriod = GetCameraTimerPeriod(RateHz);
		World->GetTimerManager().SetTimer(TimerHandle, this, &UTempoCamera::MaybeCapture, TimerPeriod, true);
	}
}

FTextureRHIRef UTempoCamera::AcquireNextStagingTexture()
{
	check(StagingTextures.Num() > 0);
	const FTextureRHIRef& Texture = StagingTextures[NextStagingIndex];
	NextStagingIndex = (NextStagingIndex + 1) % StagingTextures.Num();
	return Texture;
}

UMaterialInstanceDynamic* UTempoCamera::GetOrCreateStitchMergeMID()
{
	if (!TextureTarget || !SharedAuxTextureTarget)
	{
		return nullptr;
	}

	const UTempoSensorsSettings* Settings = GetDefault<UTempoSensorsSettings>();
	UMaterialInterface* MergeMat = nullptr;
	if (Settings)
	{
		MergeMat = bDepthEnabled
			? Settings->GetCameraStitchMergeMaterialWithDepth().Get()
			: Settings->GetCameraStitchMergeMaterialNoDepth().Get();
	}
	if (!MergeMat)
	{
		UE_LOG(LogTempoCamera, Error, TEXT("Camera stitch merge material is not set in TempoSensors settings (bDepthEnabled=%d)."), bDepthEnabled);
		return nullptr;
	}

	// Rebuild MergeMID if the source material changed (e.g., bDepthEnabled toggled).
	if (!MergeMID || MergeMID->Parent != MergeMat)
	{
		MergeMID = UMaterialInstanceDynamic::Create(MergeMat, this);
	}

	// ColorRT is the proxy capture's tonemapped LDR output (the inherited TextureTarget).
	MergeMID->SetTextureParameterValue(TEXT("ColorRT"), TextureTarget);
	MergeMID->SetTextureParameterValue(TEXT("AuxRT"), SharedAuxTextureTarget);
	return MergeMID;
}

UMaterialInstanceDynamic* UTempoCamera::GetOrCreateAuxAtlasMID()
{
	if (!SharedTextureTarget)
	{
		return nullptr;
	}

	if (!AuxAtlasMID)
	{
		const UTempoSensorsSettings* Settings = GetDefault<UTempoSensorsSettings>();
		UMaterialInterface* AuxMat = Settings ? Settings->GetCameraStitchAuxMaterial().Get() : nullptr;
		if (!AuxMat)
		{
			UE_LOG(LogTempoCamera, Error, TEXT("CameraStitchAuxMaterial is not set in TempoSensors settings."));
			return nullptr;
		}
		AuxAtlasMID = UMaterialInstanceDynamic::Create(AuxMat, this);
	}

	// The existing aux material reads "TileRT.a" at its UV. By binding TileRT to the whole atlas
	// and drawing a Canvas tile that covers (0,0)..(SizeXY) with UV (0,0)..(1,1), the material
	// unpacks the packed alpha across the entire atlas in a single pass.
	AuxAtlasMID->SetTextureParameterValue(TEXT("TileRT"), SharedTextureTarget);
	return AuxAtlasMID;
}

UMaterialInstanceDynamic* UTempoCamera::GetOrCreateProxyTonemapMID()
{
	if (!SharedTextureTarget)
	{
		return nullptr;
	}

	const UTempoSensorsSettings* Settings = GetDefault<UTempoSensorsSettings>();
	UMaterialInterface* ProxyMat = Settings ? Settings->GetCameraProxyTonemapMaterial().Get() : nullptr;
	if (!ProxyMat)
	{
		UE_LOG(LogTempoCamera, Error, TEXT("CameraProxyTonemapMaterial is not set in TempoSensors settings."));
		return nullptr;
	}

	if (!ProxyTonemapMID || ProxyTonemapMID->Parent != ProxyMat)
	{
		// Remove any stale entry from WeightedBlendables before rebuilding so we don't leak
		// references to destroyed MIDs or stack multiple copies across reconfigurations.
		if (ProxyTonemapMID)
		{
			PostProcessSettings.WeightedBlendables.Array.RemoveAll([this](const FWeightedBlendable& WB)
			{
				return WB.Object == ProxyTonemapMID;
			});
		}
		ProxyTonemapMID = UMaterialInstanceDynamic::Create(ProxyMat, this);
		PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0, ProxyTonemapMID));
	}

	ProxyTonemapMID->SetTextureParameterValue(TEXT("HDRColorRT"), SharedTextureTarget);
	return ProxyTonemapMID;
}

FTextureRead* UTempoCamera::MakeTextureRead() const
{
	// UTempoCamera manages its own readback (ShouldManageOwnReadback returns false), so this
	// override is only here to satisfy the pure-virtual contract of UTempoSceneCaptureComponent2D.
	// FTextureReads are actually constructed inline in MaybeCapture, where CameraPixelWithDepth vs
	// NoDepth is selected based on the current bDepthEnabled.
	checkNoEntry();
	return nullptr;
}

int32 UTempoCamera::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
}

void UTempoCamera::InitRenderTarget()
{
	if (SizeXY.X <= 0 || SizeXY.Y <= 0)
	{
		return;
	}

	// Super::InitRenderTarget allocates the inherited TextureTarget (LDR RGBA8, gamma 2.2) which
	// is the proxy's tonemapped output; because ShouldManageOwnReadback() returns false it does
	// not allocate staging textures or a distortion map, leaving both for us to manage.
	Super::InitRenderTarget();

	// SharedTextureTarget is the atlas: one tile-per-view family renders directly into it, and
	// it also feeds the proxy tonemap PPM (HDRColorRT). Format is fp32 — not for color dynamic
	// range, but because each tile's post-process material bit-packs (label, depth) into the
	// alpha channel, and fp16 alpha does not have the mantissa bits to preserve that. Point
	// sampling is mandatory for the aux unpack pass — bilinear would smear adjacent pixels'
	// bit patterns and break asuint() decoding.
	SharedTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedTextureTarget->TargetGamma = 1.0f;
	SharedTextureTarget->bGPUSharedFlag = true;
	SharedTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA32f;
	SharedTextureTarget->Filter = TextureFilter::TF_Nearest;
	SharedTextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);

	// Aux RT carries packed label+depth bytes produced by the aux stitch pass. RGBA8 regardless
	// of bDepthEnabled (the aux channels are byte-packed integer data, not HDR color).
	SharedAuxTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedAuxTextureTarget->TargetGamma = GetDefault<UTempoSensorsSettings>()->GetSceneCaptureGamma();
	SharedAuxTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	SharedAuxTextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);

	InitFinalRenderTargetAndStaging();
}

void UTempoCamera::InitFinalRenderTargetAndStaging()
{
	if (SizeXY.X <= 0 || SizeXY.Y <= 0)
	{
		return;
	}

	// Wait for any previous staging texture init render command to complete before modifying
	// StagingTextures, since the render command accesses the array via raw pointer.
	TextureInitFence.Wait();

	// SharedFinalTextureTarget format matches the staging / FCameraPixel layout: 4 bytes for
	// color+label, plus (when depth is enabled) 4 more bytes for discretized depth.
	const ETextureRenderTargetFormat FinalFormat = bDepthEnabled
		? ETextureRenderTargetFormat::RTF_RGBA16f
		: ETextureRenderTargetFormat::RTF_RGBA8;
	const EPixelFormat FinalPixelFormatOverride = bDepthEnabled
		? EPixelFormat::PF_A16B16G16R16
		: EPixelFormat::PF_Unknown;

	// Final merge RT, fed by the merge material. Format matches staging / FCameraPixel layout.
	SharedFinalTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedFinalTextureTarget->TargetGamma = GetDefault<UTempoSensorsSettings>()->GetSceneCaptureGamma();
	SharedFinalTextureTarget->bGPUSharedFlag = true;
	SharedFinalTextureTarget->RenderTargetFormat = FinalFormat;
	if (FinalPixelFormatOverride == EPixelFormat::PF_Unknown)
	{
		SharedFinalTextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);
	}
	else
	{
		SharedFinalTextureTarget->InitCustomFormat(SizeXY.X, SizeXY.Y, FinalPixelFormatOverride, true);
	}

	const int32 MaxQueueSize = GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
	const int32 NumStagingTextures = FMath::Max(2, MaxQueueSize > 0 ? MaxQueueSize + 1 : 2);

	{
		FScopeLock Lock(&StagingTexturesMutex);
		if (NumStagingTextures != StagingTextures.Num())
		{
			StagingTextures.SetNum(NumStagingTextures);
			NextStagingIndex = 0;
		}
	}

	struct FInitStagingContext
	{
		FString NameBase;
		int32 SizeX;
		int32 SizeY;
		EPixelFormat PixelFormat;
		int32 NumTextures;
		TArray<FTextureRHIRef>* StagingTextures;
		FCriticalSection* StagingTexturesMutex;
	};

	FInitStagingContext Context = {
		GetName(),
		SharedFinalTextureTarget->SizeX,
		SharedFinalTextureTarget->SizeY,
		SharedFinalTextureTarget->GetFormat(),
		NumStagingTextures,
		&StagingTextures,
		&StagingTexturesMutex
	};

	ENQUEUE_RENDER_COMMAND(InitTempoCameraSharedTextureCopy)(
		[Context](FRHICommandListImmediate& RHICmdList)
		{
			constexpr ETextureCreateFlags TexCreateFlags = ETextureCreateFlags::Shared | ETextureCreateFlags::CPUReadback;

			for (int32 I = 0; I < Context.NumTextures; ++I)
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2D(*FString::Printf(TEXT("%s SharedStagingTexture %d"), *Context.NameBase, I))
					.SetExtent(Context.SizeX, Context.SizeY)
					.SetFormat(Context.PixelFormat)
					.SetFlags(TexCreateFlags);

				{
					FScopeLock Lock(Context.StagingTexturesMutex);
					(*Context.StagingTextures)[I] = RHICreateTexture(Desc);
				}
			}
		});

	TextureInitFence.BeginFence();

	// Any pending texture reads might have the wrong pixel format.
	TextureReadQueue.Empty();
}

void UTempoCamera::MaybeCapture()
{
	const float TimerPeriod = GetCameraTimerPeriod(RateHz);
	if (UWorld* World = GetWorld())
	{
		if (!FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRate(TimerHandle), TimerPeriod))
		{
			RestartCaptureTimer();
		}
	}

	if (!HasPendingCameraRequests())
	{
		return;
	}

	if (!SharedTextureTarget)
	{
		return;
	}

	int32 NumActiveTiles = 0;
	for (const FTempoCameraTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			++NumActiveTiles;
		}
	}
	if (NumActiveTiles == 0)
	{
		return;
	}

	const int32 MaxQueueSize = GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
	if (MaxQueueSize > 0 && TextureReadQueue.Num() > MaxQueueSize)
	{
		UE_LOG(LogTempoCamera, Warning, TEXT("Fell behind while reading frames from sensor %s. Skipping capture."), *GetName());
		return;
	}

	if (!SharedFinalTextureTarget)
	{
		return;
	}

	FTextureRenderTargetResource* SharedRTResource = SharedFinalTextureTarget->GameThread_GetRenderTargetResource();
	if (!SharedRTResource)
	{
		return;
	}

	UWorld* World = GetWorld();
	FSceneInterface* Scene = World ? World->Scene : nullptr;
	if (!Scene)
	{
		return;
	}

	// Per-tile view origin (shared across tiles) — the camera's world location.
	const FTransform CameraWorld = GetComponentToWorld();
	const FVector ViewLocation = CameraWorld.GetTranslation();
	const FQuat CameraWorldRotation = CameraWorld.GetRotation();

	// Axis swap for UE view rotation convention: view x = world z, view y = world x, view z = world y.
	const FMatrix ViewAxisSwap(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	// Build one FSceneViewFamily containing all tile views and render it into SharedTextureTarget
	// (the atlas) via TempoMultiViewCapture::RenderTiles. Each tile contributes its own view state
	// (TAA/AE history) and post-process; they share the family's shadow setup, scene uniform
	// buffer, Lumen/RT wire-up, and GPU scene update.
	TArray<TempoMultiViewCapture::FViewSetup> ViewSetups;
	ViewSetups.Reserve(NumActiveTiles);
	for (FTempoCameraTile& Tile : Tiles)
	{
		if (!Tile.bActive)
		{
			continue;
		}

		if (bHasValidSharedExposure)
		{
			Tile.PostProcessSettings.bOverride_AutoExposureBias = true;
			Tile.PostProcessSettings.AutoExposureBias = SharedExposureBias;
		}

		// Tile world rotation = camera world rotation * tile relative rotation; the view matrix is
		// the inverse of that quat, followed by the capture-view axis swap.
		const FQuat TileWorldRotation = CameraWorldRotation * Tile.RelativeRotation.Quaternion();
		FMatrix ViewRotationMatrix = FQuatRotationMatrix(TileWorldRotation.Inverse()) * ViewAxisSwap;

		// Perspective projection — mirrors BuildProjectionMatrix at SceneCaptureRendering.cpp:566.
		const float UnscaledFOV = Tile.FOVAngle * (float)PI / 360.0f;
		const float ViewFOV = FMath::Atan((1.0f + Overscan) * FMath::Tan(UnscaledFOV));
		const float NearClip = bOverride_CustomNearClippingPlane ? CustomNearClippingPlane : GNearClippingPlane;
		const FIntPoint TileSize = Tile.TileOutputSizeXY;
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
		Setup.ViewRect = FIntRect(Tile.TileDestOffset, Tile.TileDestOffset + TileSize);
		Setup.FOV = Tile.FOVAngle;
	}

	// Tile render uses HDR scene color (pre-tonemap); the primary's own capture source (LDR) is
	// for the proxy tonemap pass below. The family also wants the tile-appropriate show flags
	// (no bloom/DOF/motionblur/etc) which differ from the proxy's — swap them around the call.
	{
		const FEngineShowFlags SavedShowFlags = ShowFlags;
		ShowFlags.SetLocalExposure(false);
		ShowFlags.SetEyeAdaptation(false);
		ShowFlags.SetMotionBlur(false);
		ShowFlags.SetLensFlares(false);
		ShowFlags.SetBloom(false);
		ShowFlags.SetColorGrading(false);
		ShowFlags.SetVignette(false);
		ShowFlags.SetDepthOfField(false);

		TempoMultiViewCapture::RenderTiles(Scene, this, SharedTextureTarget, ViewSetups, ESceneCaptureSource::SCS_FinalColorHDR);

		ShowFlags = SavedShowFlags;
	}

	// Build the FTextureRead for the stitched output, sized to the camera's final SizeXY.
	TMap<uint8, uint8> InstanceToSemanticMap;
	if (UTempoActorLabeler* Labeler = GetWorld()->GetSubsystem<UTempoActorLabeler>())
	{
		InstanceToSemanticMap = Labeler->GetInstanceToSemanticIdMap();
	}

	FTextureRead* NewRead = bDepthEnabled
		? static_cast<FTextureRead*>(new TTextureRead<FCameraPixelWithDepth>(
			SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(),
			GetComponentTransform(), MinDepth, MaxDepth, MoveTemp(InstanceToSemanticMap)))
		: static_cast<FTextureRead*>(new TTextureRead<FCameraPixelNoDepth>(
			SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(),
			GetComponentTransform(), MoveTemp(InstanceToSemanticMap)));

	NewRead->StagingTexture = AcquireNextStagingTexture();

	SequenceId++;

	const FTextureRHIRef StagingTex = NewRead->StagingTexture;

	// Single full-screen aux unpack pass: samples SharedTextureTarget.a across the whole atlas and
	// writes label+depth bytes into SharedAuxTextureTarget. Replaces the legacy N per-tile draws
	// — the shader logic is unchanged, only the source texture (atlas) and draw extent differ.
	if (SharedAuxTextureTarget)
	{
		if (UMaterialInstanceDynamic* AuxMID = GetOrCreateAuxAtlasMID())
		{
			UCanvas* Canvas = nullptr;
			FVector2D CanvasSize(0.0, 0.0);
			FDrawToRenderTargetContext Ctx;
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, SharedAuxTextureTarget, Canvas, CanvasSize, Ctx);

			FCanvasTileItem Item(
				FVector2D::ZeroVector,
				AuxMID->GetRenderProxy(),
				FVector2D(SizeXY.X, SizeXY.Y),
				FVector2D(0.0, 0.0),
				FVector2D(1.0, 1.0));
			Item.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(Item);

			UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
		}
	}

	// Proxy scene capture: the PPM (appended to PostProcessSettings.WeightedBlendables by
	// GetOrCreateProxyTonemapMID) replaces scene color with SharedTextureTarget's HDR linear
	// color before Bloom/AE/Tonemapper, so the tonemapped LDR output landing in the inherited
	// TextureTarget is effectively tonemap(stitched HDR).
	if (GetOrCreateProxyTonemapMID())
	{
		// The proxy's scene render is useless — its PPM overwrites scene color before bloom/AE. Hide
    	// world geometry and lighting so nothing gets rasterized.
    	auto PrevShowFlags = ShowFlags;
    	ShowFlags.SetAtmosphere(false);
    	ShowFlags.SetFog(false);
    	ShowFlags.SetLighting(false);
    	ShowFlags.SetDynamicShadows(false);
    	ShowFlags.SetStaticMeshes(false);
    	ShowFlags.SetSkeletalMeshes(false);
    	ShowFlags.SetLandscape(false);
    	ShowFlags.SetSkyLighting(false);
    	ShowFlags.SetTranslucency(false);
    	ShowFlags.SetParticles(false);
		ShowFlags.SetAntiAliasing(false);
		ShowFlags.SetTemporalAA(false);
		CaptureScene();
		ShowFlags = PrevShowFlags;
	}

	// Update SharedExposureBias from the proxy's AE result. GetLastAverageSceneLuminance reports
	// the *true* scene luminance (the AE shader divides out View.OneOverPreExposure before
	// measuring), so we can set the bias directly instead of integrating. Accumulating here races
	// against the proxy's own AE loop and oscillates into darkness within a few frames.
	if (FSceneViewStateInterface* ViewStateInterface = GetViewState(0))
	{
		const float Lum = ViewStateInterface->GetLastAverageSceneLuminance();
		if (Lum > 0.0f)
		{
			constexpr float TargetMidGrey = 0.18f;
			SharedExposureBias = FMath::Log2(TargetMidGrey / FMath::Max(Lum, static_cast<float>(KINDA_SMALL_NUMBER)));
			bHasValidSharedExposure = true;
		}
	}

	// Merge pass: a single full-screen Canvas draw that samples the proxy's tonemapped
	// TextureTarget (via MergeMID's "ColorRT" parameter) and SharedAuxTextureTarget through
	// the merge material and writes to SharedFinalTextureTarget.
	if (UMaterialInstanceDynamic* MergeMaterialInstance = GetOrCreateStitchMergeMID())
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize(0.0, 0.0);
		FDrawToRenderTargetContext Ctx;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, SharedFinalTextureTarget, Canvas, CanvasSize, Ctx);

		FCanvasTileItem Item(
			FVector2D::ZeroVector,
			MergeMaterialInstance->GetRenderProxy(),
			FVector2D(SizeXY.X, SizeXY.Y),
			FVector2D(0.0, 0.0),
			FVector2D(1.0, 1.0));
		Item.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Item);

		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
	}

	// Staging copy + fence: runs after the Canvas stitch via render-thread FIFO.
	ENQUEUE_RENDER_COMMAND(TempoCameraStagingCopy)(
		[SharedRTResource, StagingTex, NewRead](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SharedRT = SharedRTResource->GetRenderTargetTexture();

			RHICmdList.Transition(FRHITransitionInfo(SharedRT, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			RHICmdList.CopyTexture(SharedRT, StagingTex, FRHICopyTextureInfo());

			NewRead->RenderFence = RHICreateGPUFence(TEXT("TempoCameraRenderFence"));
			RHICmdList.WriteGPUFence(NewRead->RenderFence);
		});

	TextureReadQueue.Enqueue(NewRead);
}

// ------------------------------------------------------------------------------------
// Tile Management
// ------------------------------------------------------------------------------------

void UTempoCamera::ValidateFOV() const
{
	if (LensParameters.DistortionModel == ETempoDistortionModel::BrownConrady || LensParameters.DistortionModel == ETempoDistortionModel::Rational)
	{
		ensureMsgf(FOVAngle <= 170.0f, TEXT("%s FOVAngle %.2f exceeds max 170 degrees."),
			LensParameters.DistortionModel == ETempoDistortionModel::Rational ? TEXT("Rational") : TEXT("BrownConrady"), FOVAngle);
	}
	else
	{
		const double VerticalFOV = FOVAngle * static_cast<double>(SizeXY.Y) / static_cast<double>(SizeXY.X);
		ensureMsgf(FOVAngle <= 240.0f, TEXT("Equidistant FOVAngle %.2f exceeds max 240 degrees."), FOVAngle);
		ensureMsgf(VerticalFOV <= 240.0, TEXT("Equidistant VerticalFOV %.2f (derived) exceeds max 240 degrees."), VerticalFOV);
	}
}

void UTempoCamera::AllocateTileViewState(FTempoCameraTile& Tile)
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

void UTempoCamera::DeactivateTile(FTempoCameraTile& Tile)
{
	Tile.bActive = false;
	Tile.ViewState.Destroy();
	Tile.PostProcessMaterialInstance = nullptr;
	Tile.DistortionMap = nullptr;
	Tile.PostProcessSettings = FPostProcessSettings();
	Tile.bCameraCut = false;
}

void UTempoCamera::InitTileDistortionMap(FTempoCameraTile& Tile)
{
	if (Tile.TileOutputSizeXY.X <= 0 || Tile.TileOutputSizeXY.Y <= 0)
	{
		return;
	}

	TUniquePtr<FDistortionModel> Model = CreateDistortionModel(
		LensParameters,
		Tile.RelativeRotation.Yaw,
		Tile.RelativeRotation.Pitch);
	const float OutputHFOV = FMath::RadiansToDegrees(Tile.EquidistantTileHFOVRad);
	const FDistortionRenderConfig Config = Model->ComputeRenderConfig(Tile.TileOutputSizeXY, OutputHFOV);

	Tile.FOVAngle = Config.RenderFOVAngle;
	Tile.SizeXY = Config.RenderSizeXY;

	UTempoSceneCaptureComponent2D::CreateOrResizeDistortionMapTexture(Tile.DistortionMap, Tile.TileOutputSizeXY);
	UTempoSceneCaptureComponent2D::FillDistortionMap(Tile.DistortionMap, *Model, Tile.TileOutputSizeXY, Config.FOutput, Config.RenderSizeXY, Config.FRender);
	UTempoSceneCaptureComponent2D::ApplyDistortionMapToMaterial(Tile.PostProcessMaterialInstance, Tile.DistortionMap);
}

void UTempoCamera::SetTileDepthEnabled(FTempoCameraTile& Tile, bool bTileDepthEnabled)
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	if (bTileDepthEnabled)
	{
		if (const TObjectPtr<UMaterialInterface> MatWithDepth = TempoSensorsSettings->GetCameraPostProcessMaterialWithDepth())
		{
			Tile.PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(MatWithDepth.Get(), this);
			UTempoSceneCaptureComponent2D::ApplyDistortionMapToMaterial(Tile.PostProcessMaterialInstance, Tile.DistortionMap);
			Tile.MinDepth = GEngine->NearClipPlane;
			Tile.MaxDepth = TempoSensorsSettings->GetMaxCameraDepth();
			Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), Tile.MinDepth);
			Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), Tile.MaxDepth);
			Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempoCamera_Max_Discrete_Depth);
		}
		else
		{
			UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialWithDepth is not set in TempoSensors settings"));
			return;
		}
	}
	else
	{
		if (const TObjectPtr<UMaterialInterface> MatNoDepth = TempoSensorsSettings->GetCameraPostProcessMaterialNoDepth())
		{
			Tile.PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(MatNoDepth.Get(), this);
			UTempoSceneCaptureComponent2D::ApplyDistortionMapToMaterial(Tile.PostProcessMaterialInstance, Tile.DistortionMap);
		}
		else
		{
			UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialNoDepth is not set in TempoSensors settings"));
			return;
		}
	}

	// Look up optional label override pair.
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
}

void UTempoCamera::ApplyTilePostProcess(FTempoCameraTile& Tile)
{
	Tile.PostProcessSettings = PostProcessSettings;

	// Tiles must not do any per-tile exposure adaptation — divergent per-tile ViewStates would
	// produce mismatched brightness across the stitched image. Tonemap runs once on the proxy.
	Tile.PostProcessSettings.bOverride_AutoExposureMethod = true;
	Tile.PostProcessSettings.AutoExposureMethod = AEM_Manual;

	// Rebuild blendables from the owner's user-authored list, filter out ProxyTonemapMID (it reads
	// SharedTextureTarget, which tiles populate, and applying it inside a tile would feed stale
	// stitched content back into the tile's scene color), then append the distortion PPM.
	Tile.PostProcessSettings.WeightedBlendables.Array.RemoveAll([this](const FWeightedBlendable& WB)
	{
		return WB.Object != nullptr && WB.Object == ProxyTonemapMID;
	});
	if (Tile.PostProcessMaterialInstance)
	{
		Tile.PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0, Tile.PostProcessMaterialInstance));
	}

	if (bHasValidSharedExposure)
	{
		Tile.PostProcessSettings.bOverride_AutoExposureBias = true;
		Tile.PostProcessSettings.AutoExposureBias = SharedExposureBias;
	}
}

void UTempoCamera::ConfigureTile(FTempoCameraTile& Tile, double YawOffset, double PitchOffset, double PerspectiveFOV, const FIntPoint& TileSizeXY, const FIntPoint& TileDestOffset, bool bActivate)
{
	if (!bActivate)
	{
		if (Tile.bActive)
		{
			DeactivateTile(Tile);
		}
		return;
	}

	Tile.RelativeRotation = FRotator(PitchOffset, YawOffset, 0.0);
	Tile.TileOutputSizeXY = TileSizeXY;
	Tile.TileDestOffset = TileDestOffset;
	Tile.EquidistantTileHFOVRad = FMath::DegreesToRadians(PerspectiveFOV);
	Tile.SizeXY = TileSizeXY;                 // InitTileDistortionMap may adjust
	Tile.FOVAngle = static_cast<float>(PerspectiveFOV);

	AllocateTileViewState(Tile);
	SetTileDepthEnabled(Tile, bDepthEnabled);
	InitTileDistortionMap(Tile);
	ApplyTilePostProcess(Tile);

	// First frame with fresh view state must force a camera cut so TAA doesn't sample uninitialized
	// history.
	Tile.bCameraCut = !Tile.bActive || Tile.bCameraCut;
	Tile.bActive = true;
}

void UTempoCamera::SyncTiles()
{
	ValidateFOV();

	if (Tiles.Num() != 4)
	{
		Tiles.SetNum(4);
	}

	FTempoCameraTile& TL = Tiles[0];
	FTempoCameraTile& TR = Tiles[1];
	FTempoCameraTile& BL = Tiles[2];
	FTempoCameraTile& BR = Tiles[3];

	if (LensParameters.DistortionModel == ETempoDistortionModel::BrownConrady || LensParameters.DistortionModel == ETempoDistortionModel::Rational)
	{
		// Single capture: use TL, deactivate others
		ConfigureTile(TL, 0.0, 0.0, FOVAngle, SizeXY, FIntPoint::ZeroValue, /*bActivate=*/ true);
		ConfigureTile(TR, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, /*bActivate=*/ false);
		ConfigureTile(BL, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, /*bActivate=*/ false);
		ConfigureTile(BR, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, /*bActivate=*/ false);
	}
	else // Equidistant
	{
		const double VerticalFOV = FOVAngle * static_cast<double>(SizeXY.Y) / static_cast<double>(SizeXY.X);
		const bool bSplitHorizontal = FOVAngle > MaxPerspectiveFOVPerCapture;
		const bool bSplitVertical = VerticalFOV > MaxPerspectiveFOVPerCapture;

		if (!bSplitHorizontal && !bSplitVertical)
		{
			ConfigureTile(TL, 0.0, 0.0, FOVAngle, SizeXY, FIntPoint::ZeroValue, true);
			ConfigureTile(TR, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, false);
			ConfigureTile(BL, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, false);
			ConfigureTile(BR, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, false);
		}
		else if (bSplitHorizontal && !bSplitVertical)
		{
			const double SubFOV = FOVAngle / 2.0;
			const double YawOffset = SubFOV / 2.0;
			const int32 LeftWidth = FMath::CeilToInt32(SizeXY.X / 2.0);
			const int32 RightWidth = SizeXY.X - LeftWidth;

			ConfigureTile(TL, -YawOffset, 0.0, SubFOV, FIntPoint(LeftWidth, SizeXY.Y), FIntPoint(0, 0), true);
			ConfigureTile(TR, YawOffset, 0.0, SubFOV, FIntPoint(RightWidth, SizeXY.Y), FIntPoint(LeftWidth, 0), true);
			ConfigureTile(BL, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, false);
			ConfigureTile(BR, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, false);
		}
		else if (!bSplitHorizontal && bSplitVertical)
		{
			const double SubVertFOV = VerticalFOV / 2.0;
			const double PitchOffset = SubVertFOV / 2.0;
			const int32 TopHeight = FMath::CeilToInt32(SizeXY.Y / 2.0);
			const int32 BottomHeight = SizeXY.Y - TopHeight;

			ConfigureTile(TL, 0.0, PitchOffset, FOVAngle, FIntPoint(SizeXY.X, TopHeight), FIntPoint(0, 0), true);
			ConfigureTile(TR, 0.0, -PitchOffset, FOVAngle, FIntPoint(SizeXY.X, BottomHeight), FIntPoint(0, TopHeight), true);
			ConfigureTile(BL, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, false);
			ConfigureTile(BR, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, false);
		}
		else
		{
			const double SubHFOV = FOVAngle / 2.0;
			const double SubVFOV = VerticalFOV / 2.0;
			const double YawOffset = SubHFOV / 2.0;
			const double PitchOffset = SubVFOV / 2.0;
			const int32 LeftWidth = FMath::CeilToInt32(SizeXY.X / 2.0);
			const int32 RightWidth = SizeXY.X - LeftWidth;
			const int32 TopHeight = FMath::CeilToInt32(SizeXY.Y / 2.0);
			const int32 BottomHeight = SizeXY.Y - TopHeight;

			ConfigureTile(TL, -YawOffset, PitchOffset, SubHFOV, FIntPoint(LeftWidth, TopHeight), FIntPoint(0, 0), true);
			ConfigureTile(TR, YawOffset, PitchOffset, SubHFOV, FIntPoint(RightWidth, TopHeight), FIntPoint(LeftWidth, 0), true);
			ConfigureTile(BL, -YawOffset, -PitchOffset, SubHFOV, FIntPoint(LeftWidth, BottomHeight), FIntPoint(0, TopHeight), true);
			ConfigureTile(BR, YawOffset, -PitchOffset, SubHFOV, FIntPoint(RightWidth, BottomHeight), FIntPoint(LeftWidth, TopHeight), true);
		}
	}
}

void UTempoCamera::SetDepthEnabled(bool bDepthEnabledIn)
{
	if (bDepthEnabled == bDepthEnabledIn)
	{
		return;
	}

	UE_LOG(LogTempoCamera, Display, TEXT("Setting owner: %s camera: %s depth enabled: %d"), *GetOwnerName(), *GetSensorName(), bDepthEnabledIn);

	bDepthEnabled = bDepthEnabledIn;
	ApplyDepthEnabled();
}

void UTempoCamera::ApplyDepthEnabled()
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	MinDepth = GEngine->NearClipPlane;
	MaxDepth = TempoSensorsSettings->GetMaxCameraDepth();

	// Swap the PPM material (with/without depth) on active tiles and re-apply post-process.
	for (FTempoCameraTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			SetTileDepthEnabled(Tile, bDepthEnabled);
			UTempoSceneCaptureComponent2D::ApplyDistortionMapToMaterial(Tile.PostProcessMaterialInstance, Tile.DistortionMap);
			ApplyTilePostProcess(Tile);
		}
	}

	// Only SharedFinalTextureTarget and its staging textures are depth-dependent. Rebuilding
	// the inherited TextureTarget or the other shared RTs would invalidate the proxy capture's
	// persistent view state (TAA/AE history) and corrupt the tonemapped output.
	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitFinalRenderTargetAndStaging();
	}
}

#if WITH_EDITOR
void UTempoCamera::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(USceneCaptureComponent2D, FOVAngle) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, LensParameters) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, SizeXY) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(USceneCaptureComponent2D, PostProcessSettings) ||
		MemberPropertyName == TEXT("ShowFlagSettings") ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(USceneCaptureComponent, bUseRayTracingIfEnabled))
	{
		// Route through the same choke point as the runtime Tick path. In non-PIE editor no
		// captures are running, so this applies immediately. In PIE it is deferred until the
		// next safe window (TickComponent or end of SendMeasurements).
		bReconfigurePending = true;
		TryApplyPendingReconfigure();
	}
}
#endif
