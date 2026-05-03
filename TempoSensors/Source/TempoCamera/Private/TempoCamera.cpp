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
#include "HAL/IConsoleManager.h"
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
	PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
	PostProcessSettings.AutoExposureSpeedUp = 20.0;
	PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
	PostProcessSettings.AutoExposureSpeedDown = 20.0;
	PostProcessSettings.bOverride_AutoExposureLowPercent = true;
	PostProcessSettings.AutoExposureLowPercent = 75.0;
	PostProcessSettings.bOverride_AutoExposureHighPercent = true;
	PostProcessSettings.AutoExposureHighPercent = 85.0;

	// Lumen
	PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	PostProcessSettings.bOverride_ReflectionMethod = true;
	PostProcessSettings.ReflectionMethod = EReflectionMethod::Lumen;

	// Megalights
	PostProcessSettings.bOverride_bMegaLights = true;
	PostProcessSettings.bMegaLights = true;

	bUseRayTracingIfEnabled = true;

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

bool UTempoCamera::HasDetectedParameterChange() const
{
	return SizeXY != SizeXY_Internal
		|| FOVAngle != FOVAngle_Internal
		|| LensParameters != LensParameters_Internal
		|| FeatherPixels != FeatherPixels_Internal
		|| UpsamplingFactor != UpsamplingFactor_Internal
		|| bAutoTextureFilterType != bAutoTextureFilterType_Internal
		|| TextureFilterType != TextureFilterType_Internal;
}

ETempoTextureFilterType UTempoCamera::GetEffectiveTextureFilterType() const
{
	if (!bAutoTextureFilterType)
	{
		return TextureFilterType;
	}
	// No distortion: the perspective render and distorted output rasterize at the same scale, so
	// point sampling avoids the slight blur of bilinear without artifacts.
	if (LensParameters.LensModel == ETempoLensModel::Pinhole)
	{
		return ETempoTextureFilterType::Nearest;
	}
	// Wide equidistant fisheye: output sampling density varies sharply with angle, so bicubic's
	// wider footprint keeps detail in the dense central region.
	const bool bEquidistant =
		LensParameters.LensModel == ETempoLensModel::KannalaBrandt ||
		LensParameters.LensModel == ETempoLensModel::DoubleSphere;
	if (bEquidistant && FOVAngle > 120.0f)
	{
		return ETempoTextureFilterType::Bicubic;
	}
	return ETempoTextureFilterType::Bilinear;
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
	FeatherPixels_Internal = FeatherPixels;
	UpsamplingFactor_Internal = UpsamplingFactor;
	bAutoTextureFilterType_Internal = bAutoTextureFilterType;
	TextureFilterType_Internal = TextureFilterType;
}

bool UTempoCamera::HasPendingCameraRequests() const
{
	return !PendingColorImageRequests.IsEmpty() || !PendingLabelImageRequests.IsEmpty() || !PendingDepthImageRequests.IsEmpty() || !PendingBoundingBoxesRequests.IsEmpty();
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

	// Rebuild MergeMID if the source material changed (e.g., bDepthEnabled toggled). Retire the
	// old MID first — render commands from the previous frame's merge Canvas draw may still
	// reference it, and letting GC collect it would crash the render thread.
	if (!MergeMID || MergeMID->Parent != MergeMat)
	{
		RetirePPM(MergeMID);
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

	// The aux material samples the atlas via the resolve map: at each output pixel it picks the
	// owning tile's atlas UV (Weight >= 0.5) or the secondary's (Weight < 0.5), then reads
	// TileRT.a there with point sampling and unpacks label+depth bits. Pick-by-ownership rather
	// than blending — label is integer and depth is bit-packed, neither is safely averageable.
	AuxAtlasMID->SetTextureParameterValue(TEXT("TileRT"), SharedTextureTarget);
	AuxAtlasMID->SetTextureParameterValue(TEXT("OutputResolveMap"), OutputResolveMap);
	AuxAtlasMID->SetTextureParameterValue(TEXT("OutputResolveWeight"), OutputResolveWeight);
	return AuxAtlasMID;
}

UMaterialInstanceDynamic* UTempoCamera::GetOrCreateStitchColorMID()
{
	if (!SharedTextureTarget || !OutputResolveMap || !OutputResolveWeight)
	{
		return nullptr;
	}

	if (!StitchColorMID)
	{
		const UTempoSensorsSettings* Settings = GetDefault<UTempoSensorsSettings>();
		UMaterialInterface* StitchMat = Settings ? Settings->GetCameraStitchColorFeatherMaterial().Get() : nullptr;
		if (!StitchMat)
		{
			UE_LOG(LogTempoCamera, Error, TEXT("CameraStitchColorFeatherMaterial is not set in TempoSensors settings."));
			return nullptr;
		}
		StitchColorMID = UMaterialInstanceDynamic::Create(StitchMat, this);
	}

	StitchColorMID->SetTextureParameterValue(TEXT("AtlasRT"), SharedTextureTarget);
	StitchColorMID->SetTextureParameterValue(TEXT("OutputResolveMap"), OutputResolveMap);
	StitchColorMID->SetTextureParameterValue(TEXT("OutputResolveWeight"), OutputResolveWeight);
	return StitchColorMID;
}

UMaterialInstanceDynamic* UTempoCamera::GetOrCreateProxyTonemapMID()
{
	if (!SharedStitchHDRTextureTarget)
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
		// Remove any stale entry from WeightedBlendables and retire the old MID — render commands
		// from the previous frame's proxy CaptureScene may still reference it.
		if (ProxyTonemapMID)
		{
			PostProcessSettings.WeightedBlendables.Array.RemoveAll([this](const FWeightedBlendable& WB)
			{
				return WB.Object == ProxyTonemapMID;
			});
			RetirePPM(ProxyTonemapMID);
		}
		ProxyTonemapMID = UMaterialInstanceDynamic::Create(ProxyMat, this);
		PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0, ProxyTonemapMID));
	}

	// Bind to the post-stitch HDR target (the equidistant output, sized SizeXY), not the raw atlas
	// — by the time the proxy capture runs, the stitch+feather Canvas pass has resolved the
	// per-tile distorted outputs into a single equidistant HDR image.
	ProxyTonemapMID->SetTextureParameterValue(TEXT("HDRColorRT"), SharedStitchHDRTextureTarget);
	return ProxyTonemapMID;
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

	// SyncTiles must run before InitRenderTarget — it computes AtlasSize. Fall back to SizeXY in
	// edge cases where AtlasSize wasn't populated (e.g., InitRenderTarget called before BeginPlay
	// has run SyncTiles, or all tiles inactive).
	const FIntPoint AtlasSizeForRT = (AtlasSize.X > 0 && AtlasSize.Y > 0) ? AtlasSize : SizeXY;

	// Atlas RT is sized in K× pixels; per-tile geometry (TileDestOffset, TileOutputSizeXY,
	// AtlasSize) is kept in 1× output-domain pixels and scaled by K only when laying out the
	// per-tile ViewRect in RenderCapture.
	const float K = FMath::Max(1.0f, UpsamplingFactor);
	const FIntPoint AtlasSizeForRT_K(
		FMath::CeilToInt(K * AtlasSizeForRT.X),
		FMath::CeilToInt(K * AtlasSizeForRT.Y));

	// SharedTextureTarget is the atlas: one tile-per-view family renders directly into it, and
	// it feeds the stitch+feather Canvas pass that writes to SharedStitchHDRTextureTarget. Format
	// is fp32 — not for color dynamic range, but because each tile's distortion PPM bit-packs
	// (label, depth) into the alpha channel, and fp16 alpha does not have the mantissa bits to
	// preserve that. Point sampling is mandatory for the aux unpack pass; the color stitch
	// material overrides to bilinear at its sampler. Atlas dimensions are K * AtlasSize, which
	// may be larger than SizeXY when feathering (each tile gets a disjoint atlas region) and is
	// further multiplied by UpsamplingFactor to give the perspective render denser pixels for
	// the distortion PPM to resample. The stitch pass downsamples to SizeXY via bilinear.
	SharedTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedTextureTarget->TargetGamma = 1.0f;
	SharedTextureTarget->bGPUSharedFlag = true;
	SharedTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA32f;
	SharedTextureTarget->Filter = TextureFilter::TF_Nearest;
	SharedTextureTarget->InitAutoFormat(AtlasSizeForRT_K.X, AtlasSizeForRT_K.Y);

	// Stitch HDR RT: the equidistant-output-sized linear HDR target written by the stitch+feather
	// Canvas pass and read by the proxy tonemap PPM as scene color. Sized to SizeXY so the proxy's
	// canvas mapping is identity.
	SharedStitchHDRTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedStitchHDRTextureTarget->TargetGamma = 1.0f;
	SharedStitchHDRTextureTarget->bGPUSharedFlag = true;
	SharedStitchHDRTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	SharedStitchHDRTextureTarget->Filter = TextureFilter::TF_Bilinear;
	SharedStitchHDRTextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);

	// Aux RT carries packed label+depth bytes produced by the aux stitch pass. RGBA8 regardless
	// of bDepthEnabled (the aux channels are byte-packed integer data, not HDR color).
	SharedAuxTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedAuxTextureTarget->TargetGamma = GetDefault<UTempoSensorsSettings>()->GetSceneCaptureGamma();
	SharedAuxTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	SharedAuxTextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);

	// Resolve maps describe (per equidistant-output pixel) which tile(s) cover it, where in the
	// atlas to sample, and how to feather. PF_FloatRGBA holds the two atlas UVs; PF_R16F holds the
	// tile-A weight. Both use TF_Nearest because the materials need exact per-output-pixel data
	// (smearing the UVs across pixel boundaries would create spurious atlas reads at seams).
	OutputResolveMap = UTexture2D::CreateTransient(SizeXY.X, SizeXY.Y, PF_FloatRGBA);
	OutputResolveMap->CompressionSettings = TC_HDR;
	OutputResolveMap->Filter = TF_Nearest;
	OutputResolveMap->AddressX = TA_Clamp;
	OutputResolveMap->AddressY = TA_Clamp;
	OutputResolveMap->SRGB = 0;
	OutputResolveMap->UpdateResource();

	OutputResolveWeight = UTexture2D::CreateTransient(SizeXY.X, SizeXY.Y, PF_R16F);
	OutputResolveWeight->CompressionSettings = TC_HDR;
	OutputResolveWeight->Filter = TF_Nearest;
	OutputResolveWeight->AddressX = TA_Clamp;
	OutputResolveWeight->AddressY = TA_Clamp;
	OutputResolveWeight->SRGB = 0;
	OutputResolveWeight->UpdateResource();

	// Fill the resolve maps from current tile geometry.
	RebuildResolveMaps();

	InitFinalRenderTargetAndStaging();
}

void UTempoCamera::InitFinalRenderTargetAndStaging()
{
	if (SizeXY.X <= 0 || SizeXY.Y <= 0)
	{
		return;
	}

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

	AllocateStagingTextures(SharedFinalTextureTarget->SizeX, SharedFinalTextureTarget->SizeY, SharedFinalTextureTarget->GetFormat());

	// Any pending texture reads might have the wrong pixel format.
	TextureReadQueue.Empty();
}

int32 UTempoCamera::GetNumActiveTiles() const
{
	int32 Count = 0;
	for (const FTempoCameraTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			++Count;
		}
	}
	return Count;
}

void UTempoCamera::RenderCapture()
{
	// Camera-specific guard: the fast path renders directly to SharedFinalTextureTarget and the
	// multi-tile path reads back from it; either way its resource must be valid.
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

	int32 NumActiveTiles = 0;
	for (const FTempoCameraTile& Tile : Tiles)
	{
		if (Tile.bActive)
		{
			++NumActiveTiles;
		}
	}

	// Single-tile fast path: when there's exactly one active tile, depth packing isn't needed,
	// and no upsampling is requested, render with full post-process (bloom/AE/tonemap on)
	// directly to SharedFinalTextureTarget, skipping the aux unpack, proxy tonemap capture, and
	// merge passes. Saves a separate FSceneRenderer pass plus two Canvas blits per frame; only
	// viable for !bDepthEnabled because the depth-with-label encoding bit-packs into HDR alpha
	// and can't survive LDR quantization, and only for UpsamplingFactor == 1 because the fast
	// path writes straight to the 1× final RT with no place to do the K→1 downsample.
	const bool bSingleTileFastPath = (NumActiveTiles == 1) && !bDepthEnabled && UpsamplingFactor == 1.0f;

	// Force camera cut on path-mode transition: the active tile's TAA/AE history was conditioned
	// on a different show-flag set, so reusing it would alias.
	if (bSingleTileFastPath != bWasSingleTileFastPath)
	{
		for (FTempoCameraTile& Tile : Tiles)
		{
			if (Tile.bActive)
			{
				Tile.bCameraCut = true;
			}
		}
	}
	bWasSingleTileFastPath = bSingleTileFastPath;

	// Per-tile geometry (TileDestOffset, TileOutputSizeXY) is in 1× output pixels; the atlas RT
	// is sized at K× output. Round each ViewRect *edge* through K so adjacent tiles' rects share
	// their boundary exactly (rounding offset and size independently could leave a gap or overlap
	// on the seam).
	const float K = FMath::Max(1.0f, UpsamplingFactor);

	float SavedTSRShadingRejectionExposureOffset = 0.0f;
	IConsoleVariable* TSRShadingRejectionExposureOffsetCVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.TSR.ShadingRejection.ExposureOffset"));
	if (!bSingleTileFastPath)
	{
		// Push `r.TSR.ShadingRejection.ExposureOffset` for the duration of this camera's renders.
		// CVar mutation must happen on the game thread — calling Set() on the render thread hits a
		// check(0) in FConsoleManager::OnCVarChange. The GT path propagates the new value to the RT
		// shadow via an ENQUEUE_RENDER_COMMAND queued in FIFO order with subsequent renderer commands,
		// so the bracket on the render thread is set→render→restore. Multiple TempoCameras in the
		// same frame interleave correctly because each camera's GT push, renders, and GT pop produce
		// three RT entries in that order. ECVF_SetByConsole is the highest priority and always wins,
		// avoiding the engine warning about replacing constructor-default priority and silently being
		// rejected if anything else has touched the CVar.
		if (TSRShadingRejectionExposureOffsetCVar)
		{
			SavedTSRShadingRejectionExposureOffset = TSRShadingRejectionExposureOffsetCVar->GetFloat();
			// 2.0 multiplier determined to work well empirically...
			TSRShadingRejectionExposureOffsetCVar->Set(2.0f * SharedExposureBias, ECVF_SetByConsole);
		}
	}

	IConsoleVariable* TSRShadingRejectionFlickingFrameRateCapCVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.TSR.ShadingRejection.Flickering.FrameRateCap"));
	const float SavedTSRShadingRejectionFlickingFrameRateCap = TSRShadingRejectionFlickingFrameRateCapCVar->GetFloat();
	TSRShadingRejectionFlickingFrameRateCapCVar->Set(RateHz, ECVF_SetByConsole);

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

	// Fast-path PP: a copy of the component's user-authored PostProcessSettings (so the user's
	// AE/Bloom/color-grading actually take effect — the tile's PP is bare except for the distortion
	// PPM and the manual-AE override used in multi-tile mode). Strip the proxy tonemap PPM (which
	// samples SharedTextureTarget — unused in fast-path) and layer the tile's distortion PPM on top.
	// Stack-local so it lives until RenderTiles returns; FViewSetup holds a pointer.
	FPostProcessSettings FastPathPP;
	if (bSingleTileFastPath)
	{
		FastPathPP = PostProcessSettings;
		if (ProxyTonemapMID)
		{
			FastPathPP.WeightedBlendables.Array.RemoveAll([this](const FWeightedBlendable& WB)
			{
				return WB.Object == ProxyTonemapMID;
			});
		}
	}

	// Build one FSceneViewFamily containing all tile views and render it into SharedTextureTarget
	// (the atlas) via TempoMultiViewCapture::RenderTiles. Each tile contributes its own view state
	// (TAA/AE history) and post-process; they share the family's shadow setup, scene uniform
	// buffer, Lumen/RT wire-up, and GPU scene update.
	TArray<TempoMultiViewCapture::FViewSetup> ViewSetups;
	ViewSetups.Reserve(NumActiveTiles);
	FTempoCameraTile* SingleActiveTile = nullptr;
	for (FTempoCameraTile& Tile : Tiles)
	{
		if (!Tile.bActive)
		{
			continue;
		}
		SingleActiveTile = &Tile;

		// Multi-tile must run AEM_Manual + a shared EV bias so tiles don't diverge in brightness.
		// Re-set every frame so it's clean if we just transitioned out of fast-path.
		if (!bSingleTileFastPath)
		{
			Tile.PostProcessSettings.bOverride_AutoExposureMethod = true;
			Tile.PostProcessSettings.AutoExposureMethod = AEM_Manual;
			if (bHasValidSharedExposure)
			{
				Tile.PostProcessSettings.bOverride_AutoExposureBias = true;
				Tile.PostProcessSettings.AutoExposureBias = SharedExposureBias;
			}
		}

		// Switch the distortion material's label encoding: bit-packed (asuint(label) << 24) for
		// the HDR atlas path, normalized (label / 255) for direct-to-LDR fast path so the byte
		// survives RGBA8 quantization.
		if (Tile.PostProcessMaterialInstance)
		{
			Tile.PostProcessMaterialInstance->SetScalarParameterValue(
				TEXT("UseNormalizedLabel"), bSingleTileFastPath ? 1.0f : 0.0f);

			// Fast-path PP starts from the component's settings (no distortion PPM); add the tile's
			// distortion PPM here so the view picks it up alongside the user's AE/bloom/etc.
			if (bSingleTileFastPath)
			{
				FastPathPP.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Tile.PostProcessMaterialInstance));
			}
		}

		// Tile world rotation = camera world rotation * tile relative rotation; the view matrix is
		// the inverse of that quat, followed by the capture-view axis swap.
		const FQuat TileWorldRotation = CameraWorldRotation * Tile.RelativeRotation.Quaternion();
		FMatrix ViewRotationMatrix = FQuatRotationMatrix(TileWorldRotation.Inverse()) * ViewAxisSwap;

		// Off-axis perspective projection. The frustum bounds come from the tile's
		// FDistortionRenderConfig, which sized them to tightly enclose the actually-used render
		// quadrant. For symmetric (centered) tiles this reproduces FReversedZPerspectiveMatrix
		// exactly (TanLeft = -TanRight, TanTop = -TanBottom). For corner / off-axis tiles the
		// frustum collapses around the active quadrant so the rasterizer no longer wastes pixels
		// on the empty regions of a symmetric frustum — same render-target size, ~2x effective
		// resolution at the tile's outer edges. Overscan inflates symmetrically around the
		// frustum's center, matching the legacy behavior of growing tan(half-FOV) by (1 + Overscan).
		const float NearClip = bOverride_CustomNearClippingPlane ? CustomNearClippingPlane : GNearClippingPlane;
		const FIntPoint TileSize = Tile.TileOutputSizeXY;

		const double TanCenterX = 0.5 * (Tile.TanLeft + Tile.TanRight);
		const double TanCenterY = 0.5 * (Tile.TanTop + Tile.TanBottom);
		const double TanHalfW = 0.5 * (Tile.TanRight - Tile.TanLeft) * (1.0 + Overscan);
		const double TanHalfH = 0.5 * (Tile.TanBottom - Tile.TanTop) * (1.0 + Overscan);
		const double L = TanCenterX - TanHalfW;
		const double R = TanCenterX + TanHalfW;
		const double T = TanCenterY - TanHalfH;
		const double B = TanCenterY + TanHalfH;

		const double Sx = 2.0 / (R - L);
		const double Sy = 2.0 / (B - T);
		const double Cx = -(R + L) / (R - L);
		const double Cy = (T + B) / (B - T);

		FMatrix ProjectionMatrix = FMatrix::Identity;
		ProjectionMatrix.M[0][0] = static_cast<float>(Sx);
		ProjectionMatrix.M[0][1] = 0.0f;
		ProjectionMatrix.M[0][2] = 0.0f;
		ProjectionMatrix.M[0][3] = 0.0f;
		ProjectionMatrix.M[1][0] = 0.0f;
		ProjectionMatrix.M[1][1] = static_cast<float>(Sy);
		ProjectionMatrix.M[1][2] = 0.0f;
		ProjectionMatrix.M[1][3] = 0.0f;
		ProjectionMatrix.M[2][0] = static_cast<float>(Cx);
		ProjectionMatrix.M[2][1] = static_cast<float>(Cy);
		ProjectionMatrix.M[2][3] = 1.0f;
		ProjectionMatrix.M[3][0] = 0.0f;
		ProjectionMatrix.M[3][1] = 0.0f;
		ProjectionMatrix.M[3][3] = 0.0f;
		if ((int32)ERHIZBuffer::IsInverted)
		{
			// Reversed-Z infinite far plane (MinZ == MaxZ branch of FReversedZPerspectiveMatrix).
			ProjectionMatrix.M[2][2] = 0.0f;
			ProjectionMatrix.M[3][2] = NearClip;
		}
		else
		{
			// Forward-Z infinite far plane (MinZ == MaxZ branch of FPerspectiveMatrix); Z_PRECISION
			// is 0 in the engine so the (1 - Z_PRECISION) factor collapses to 1.
			ProjectionMatrix.M[2][2] = 1.0f;
			ProjectionMatrix.M[3][2] = -NearClip;
		}

		TempoMultiViewCapture::FViewSetup& Setup = ViewSetups.AddDefaulted_GetRef();
		Setup.ViewState = Tile.ViewState.GetReference();
		Setup.PostProcessSettings = bSingleTileFastPath ? &FastPathPP : &Tile.PostProcessSettings;
		Setup.PostProcessBlendWeight = 1.0f;
		Setup.bCameraCut = Tile.bCameraCut;
		Tile.bCameraCut = false;
		Setup.ViewLocation = ViewLocation;
		Setup.ViewRotationMatrix = ViewRotationMatrix;
		Setup.ProjectionMatrix = ProjectionMatrix;
		const FIntPoint ViewRectMinK(
			FMath::RoundToInt(K * Tile.TileDestOffset.X),
			FMath::RoundToInt(K * Tile.TileDestOffset.Y));
		const FIntPoint ViewRectMaxK(
			FMath::RoundToInt(K * (Tile.TileDestOffset.X + TileSize.X)),
			FMath::RoundToInt(K * (Tile.TileDestOffset.Y + TileSize.Y)));
		Setup.ViewRect = FIntRect(ViewRectMinK, ViewRectMaxK);
		Setup.FOV = Tile.FOVAngle;
	}

	const float ResolutionFraction = bEnableScreenPercentage
		? FMath::Clamp(ScreenPercentage / 100.0f, 0.25f, 2.0f)
		: 1.0f;

	if (bSingleTileFastPath)
	{
		// Single tile, full post-process: render straight to the final RT in LDR. The distortion
		// PPM packs label/255 into alpha; RGBA8 quantization preserves the byte exactly.
		TempoMultiViewCapture::RenderTiles(Scene, this, SharedFinalTextureTarget, ViewSetups, ESceneCaptureSource::SCS_FinalColorLDR, ResolutionFraction);
	}
	else
	{
		// Multi-tile: HDR atlas (pre-tonemap) so the proxy capture below can run AE/tonemap once
		// across the stitched output. Tile-appropriate show flags (no bloom/DOF/motionblur/etc)
		// differ from the proxy's — swap them around the call.
		const FEngineShowFlags SavedShowFlags = ShowFlags;
		ShowFlags.SetLocalExposure(false);
		ShowFlags.SetEyeAdaptation(false);
		ShowFlags.SetMotionBlur(false);
		ShowFlags.SetLensFlares(false);
		ShowFlags.SetBloom(false);
		ShowFlags.SetColorGrading(false);
		ShowFlags.SetVignette(false);
		ShowFlags.SetDepthOfField(false);

		TempoMultiViewCapture::RenderTiles(Scene, this, SharedTextureTarget, ViewSetups, ESceneCaptureSource::SCS_FinalColorHDR, ResolutionFraction);

		ShowFlags = SavedShowFlags;

		// Stitch + feather pass: resolves the per-tile distorted atlas into a single equidistant
		// HDR image (SharedStitchHDRTextureTarget, sized SizeXY). The resolve map drives where to
		// sample the atlas and how to blend across overlapping tile coverage near seams. The proxy
		// capture below reads this RT (via HDRColorRT) as scene color before bloom/AE/tonemap.
		if (SharedStitchHDRTextureTarget)
		{
			if (UMaterialInstanceDynamic* StitchMID = GetOrCreateStitchColorMID())
			{
				UCanvas* Canvas = nullptr;
				FVector2D CanvasSize(0.0, 0.0);
				FDrawToRenderTargetContext Ctx;
				UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, SharedStitchHDRTextureTarget, Canvas, CanvasSize, Ctx);

				FCanvasTileItem Item(
					FVector2D::ZeroVector,
					StitchMID->GetRenderProxy(),
					FVector2D(SizeXY.X, SizeXY.Y),
					FVector2D(0.0, 0.0),
					FVector2D(1.0, 1.0));
				Item.BlendMode = SE_BLEND_Opaque;
				Canvas->DrawItem(Item);

				UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
			}
		}
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

	if (!bSingleTileFastPath)
	{
		// Single full-screen aux unpack pass: samples SharedTextureTarget.a across the whole atlas
		// and writes label+depth bytes into SharedAuxTextureTarget. Replaces the legacy N per-tile
		// draws — the shader logic is unchanged, only the source texture (atlas) and draw extent
		// differ.
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
		// GetOrCreateProxyTonemapMID) replaces scene color with SharedStitchHDRTextureTarget's HDR
		// linear color before Bloom/AE/Tonemapper, so the tonemapped LDR output landing in the
		// inherited TextureTarget is effectively tonemap(stitched HDR).
		if (GetOrCreateProxyTonemapMID())
		{
			// The proxy's scene render is useless — its PPM overwrites scene color before bloom/AE. Hide
			// world geometry and lighting so nothing gets rasterized.
			auto PrevShowFlags = ShowFlags;
			ShowFlags.SetAtmosphere(false);
			ShowFlags.SetFog(false);
			ShowFlags.SetDynamicShadows(false);
			ShowFlags.SetStaticMeshes(false);
			ShowFlags.SetSkeletalMeshes(false);
			ShowFlags.SetLandscape(false);
			ShowFlags.SetSkyLighting(false);
			ShowFlags.SetTranslucency(false);
			ShowFlags.SetParticles(false);
			ShowFlags.SetAntiAliasing(false);
			ShowFlags.SetTemporalAA(false);

			// But wait! Lighting actually has to be on in order for exposure bias to be read back on the cpu
			ShowFlags.SetLighting(true);
			CaptureScene();
			ShowFlags = PrevShowFlags;
		}
	}

	// Update SharedExposureBias from whichever view state ran the AE pass this frame.
	{
		FSceneViewStateInterface* SourceViewState = bSingleTileFastPath
			? (SingleActiveTile ? SingleActiveTile->ViewState.GetReference() : nullptr)
			: GetViewState(0);
		if (SourceViewState)
		{
			const float Lum = SourceViewState->GetLastAverageSceneLuminance();
			if (Lum > 0.0f)
			{
				constexpr float TargetMidGrey = 0.18f;
				SharedExposureBias = FMath::Log2(TargetMidGrey / FMath::Max(Lum, static_cast<float>(KINDA_SMALL_NUMBER)));
				bHasValidSharedExposure = true;
			}
		}
	}

	if (!bSingleTileFastPath)
	{
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

	// Pop: restore the prior CVar value. The propagation render command queued by Set() on
	// GT lands in RT FIFO after every render command this camera enqueued above, so each
	// camera's renders consume the camera's own override before the restore takes effect.
	if (!bSingleTileFastPath)
	{
		if (TSRShadingRejectionExposureOffsetCVar)
		{
			TSRShadingRejectionExposureOffsetCVar->Set(SavedTSRShadingRejectionExposureOffset, ECVF_SetByConsole);
		}
	}

	if (TSRShadingRejectionFlickingFrameRateCapCVar)
	{
		TSRShadingRejectionFlickingFrameRateCapCVar->Set(SavedTSRShadingRejectionFlickingFrameRateCap, ECVF_SetByConsole);
	}

	TextureReadQueue.Enqueue(NewRead);
}

// ------------------------------------------------------------------------------------
// Tile Management
// ------------------------------------------------------------------------------------

void UTempoCamera::ValidateFOV() const
{
	auto Report = [this](int32 KeySalt, const FString& Message)
	{
		UE_LOG(LogTempoCamera, Error, TEXT("%s"), *Message);
		if (GEngine)
		{
			const uint64 Key = (static_cast<uint64>(GetUniqueID()) << 8) | static_cast<uint64>(KeySalt);
			GEngine->AddOnScreenDebugMessage(static_cast<int32>(Key), 5.0f, FColor::Red, Message);
		}
	};

	if (LensParameters.LensModel == ETempoLensModel::Pinhole ||
		LensParameters.LensModel == ETempoLensModel::BrownConrady ||
		LensParameters.LensModel == ETempoLensModel::Rational)
	{
		const TCHAR* ModelName = TEXT("Pinhole");
		if (LensParameters.LensModel == ETempoLensModel::Rational) ModelName = TEXT("Rational");
		else if (LensParameters.LensModel == ETempoLensModel::BrownConrady) ModelName = TEXT("BrownConrady");

		if (FOVAngle > 170.0f)
		{
			Report(0, FString::Printf(TEXT("%s FOVAngle %.2f exceeds max 170 degrees."), ModelName, FOVAngle));
		}

		// Barrel distortion peaks at a critical source radius beyond which forward distortion is
		// non-monotonic and the inverse Newton-Raphson solve cannot converge. Translate that
		// source-side limit (along the diagonal — the longest axis through the image plane) back
		// into a maximum representable horizontal FOV.
		double MaxRenderRad = -1.0;
		if (LensParameters.LensModel == ETempoLensModel::BrownConrady)
		{
			MaxRenderRad = FBrownConradyDistortion::ComputeMaxRenderRadius(
				LensParameters.K1, LensParameters.K2, LensParameters.K3);
		}
		else if (LensParameters.LensModel == ETempoLensModel::Rational)
		{
			MaxRenderRad = FRationalDistortion::ComputeMaxRenderRadius(
				LensParameters.K1, LensParameters.K2, LensParameters.K3,
				LensParameters.K4, LensParameters.K5, LensParameters.K6);
		}
		if (MaxRenderRad > 0.0 && SizeXY.X > 0 && SizeXY.Y > 0)
		{
			const double AspectRatio = static_cast<double>(SizeXY.X) / static_cast<double>(SizeXY.Y);
			const double DiagOverHoriz = FMath::Sqrt(1.0 + 1.0 / (AspectRatio * AspectRatio));
			const double MaxHorizSourceRad = MaxRenderRad / DiagOverHoriz;
			const double MaxFOVDeg = 2.0 * FMath::RadiansToDegrees(FMath::Atan(MaxHorizSourceRad));
			if (FOVAngle > MaxFOVDeg)
			{
				Report(7, FString::Printf(TEXT("%s FOVAngle %.2f exceeds parameter-implied max %.2f. Inverse distortion (Newton-Raphson) cannot converge for some pixels with the current K coefficients."), ModelName, FOVAngle, MaxFOVDeg));
			}
		}
	}
	else if (LensParameters.LensModel == ETempoLensModel::DoubleSphere)
	{
		const double VerticalFOV = FOVAngle * static_cast<double>(SizeXY.Y) / static_cast<double>(SizeXY.X);
		if (FOVAngle > 280.0f)
		{
			Report(1, FString::Printf(TEXT("DoubleSphere FOVAngle %.2f exceeds max 280 degrees."), FOVAngle));
		}
		if (VerticalFOV > 280.0)
		{
			Report(2, FString::Printf(TEXT("DoubleSphere VerticalFOV %.2f (derived) exceeds max 280 degrees."), VerticalFOV));
		}
		if (LensParameters.Alpha < 0.0f || LensParameters.Alpha > 1.0f)
		{
			Report(3, FString::Printf(TEXT("DoubleSphere Alpha %.3f outside [0, 1]."), LensParameters.Alpha));
		}
		if (LensParameters.Xi < -1.0f || LensParameters.Xi > 1.0f)
		{
			Report(4, FString::Printf(TEXT("DoubleSphere Xi %.3f outside [-1, 1]."), LensParameters.Xi));
		}
	}
	else
	{
		const double VerticalFOV = FOVAngle * static_cast<double>(SizeXY.Y) / static_cast<double>(SizeXY.X);
		if (FOVAngle > 240.0f)
		{
			Report(5, FString::Printf(TEXT("Equidistant FOVAngle %.2f exceeds max 240 degrees."), FOVAngle));
		}
		if (VerticalFOV > 240.0)
		{
			Report(6, FString::Printf(TEXT("Equidistant VerticalFOV %.2f (derived) exceeds max 240 degrees."), VerticalFOV));
		}
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

void UTempoCamera::RetireDistortionMap(UTexture2D* DistortionMap)
{
	if (DistortionMap)
	{
		RetainedDistortionMaps.AddUnique(DistortionMap);
		const int32 MaxSize = GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
		while (RetainedDistortionMaps.Num() > MaxSize)
		{
			RetainedDistortionMaps.RemoveAt(0);
		}
	}
}

void UTempoCamera::DeactivateTile(FTempoCameraTile& Tile)
{
	Tile.bActive = false;
	Tile.ViewState.Destroy();
	// Retire the PPM + distortion map instead of nulling: render commands from prior captures
	// may still reference them, and dropping the only UPROPERTY reference lets GC flag them
	// as "about to be deleted" mid-render (FMaterialRenderProxy::CacheUniformExpressions asserts).
	RetirePPM(Tile.PostProcessMaterialInstance);
	RetireDistortionMap(Tile.DistortionMap);
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

	TUniquePtr<FLensModel> Model = CreateLensModel(
		LensParameters,
		Tile.RelativeRotation.Yaw,
		Tile.RelativeRotation.Pitch,
		Tile.AxisShiftXRd,
		Tile.AxisShiftYRd);
	const FDistortionRenderConfig Config = Model->ComputeRenderConfig(Tile.TileOutputSizeXY, Tile.OutputFocalLength);

	Tile.FOVAngle = Config.RenderFOVAngle;
	Tile.SizeXY = Config.RenderSizeXY;
	Tile.TanLeft = Config.TanLeft;
	Tile.TanRight = Config.TanRight;
	Tile.TanTop = Config.TanTop;
	Tile.TanBottom = Config.TanBottom;

	// CreateOrResizeDistortionMapTexture overwrites Tile.DistortionMap — retire the previous one
	// first so in-flight render commands don't see it marked-for-GC.
	RetireDistortionMap(Tile.DistortionMap);
	Tile.DistortionMap = nullptr;
	UTempoSceneCaptureComponent2D::CreateOrResizeDistortionMapTexture(Tile.DistortionMap, Tile.TileOutputSizeXY);
	UTempoSceneCaptureComponent2D::FillDistortionMap(Tile.DistortionMap, *Model, Tile.TileOutputSizeXY, Config.FOutput,
		Config.RenderSizeXY, Config.TanLeft, Config.TanRight, Config.TanTop, Config.TanBottom);
	UTempoSceneCaptureComponent2D::ApplyDistortionMapToMaterial(Tile.PostProcessMaterialInstance, Tile.DistortionMap);

	// Push the tile's tan-bounds onto the distortion PPM. The depth path uses these to recover the
	// view-space ray direction at the resampled UV (tan_x = lerp(TanLeft,TanRight,U_render), etc.)
	// and convert SceneDepth to radial distance when UseRadialDistance is set.
	if (Tile.PostProcessMaterialInstance)
	{
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("TanLeft"), Tile.TanLeft);
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("TanRight"), Tile.TanRight);
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("TanTop"), Tile.TanTop);
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("TanBottom"), Tile.TanBottom);
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("FilterType"), static_cast<float>(GetEffectiveTextureFilterType()));
	}
}

void UTempoCamera::RebuildResolveMaps()
{
	if (!OutputResolveMap || !OutputResolveWeight)
	{
		return;
	}
	if (SizeXY.X <= 0 || SizeXY.Y <= 0 || AtlasSize.X <= 0 || AtlasSize.Y <= 0)
	{
		return;
	}

	// Snapshot active tile geometry into a flat array. The runtime loop benefits from fast
	// iteration over a contiguous container; we hit this 4-tile array up to (4 owner tests +
	// 4 neighbor probes) * SizeXY.X * SizeXY.Y times in the worst case.
	struct FTileGeom
	{
		FIntRect Owned;        // tile's slice of equidistant-output space (centerline pick)
		FIntRect Covered;      // owned + feather margins on shared edges; equals atlas tile rect (translated to output coords)
		FIntPoint AtlasOffset; // tile's offset within the atlas
	};
	TArray<FTileGeom, TInlineAllocator<4>> ActiveTiles;
	const int32 F = FMath::Max(0, FeatherPixels);
	for (const FTempoCameraTile& Tile : Tiles)
	{
		if (!Tile.bActive)
		{
			continue;
		}
		FTileGeom Geom;
		Geom.Owned = FIntRect(Tile.OwnedOutputOffset, Tile.OwnedOutputOffset + Tile.OwnedOutputSize);
		// A tile has feather margin only on edges where it has a neighbor — i.e., where its owned
		// rect doesn't touch the equidistant-output boundary. Tiles at the image edge keep their
		// flush-with-the-edge ownership (and no neighbor across that edge).
		const int32 LeftMargin = (Geom.Owned.Min.X > 0) ? F : 0;
		const int32 TopMargin = (Geom.Owned.Min.Y > 0) ? F : 0;
		const int32 RightMargin = (Geom.Owned.Max.X < SizeXY.X) ? F : 0;
		const int32 BottomMargin = (Geom.Owned.Max.Y < SizeXY.Y) ? F : 0;
		Geom.Covered.Min = Geom.Owned.Min - FIntPoint(LeftMargin, TopMargin);
		Geom.Covered.Max = Geom.Owned.Max + FIntPoint(RightMargin, BottomMargin);
		Geom.AtlasOffset = Tile.TileDestOffset;
		ActiveTiles.Add(Geom);
	}
	if (ActiveTiles.IsEmpty())
	{
		return;
	}

	FTexture2DMipMap& MipMap = OutputResolveMap->GetPlatformData()->Mips[0];
	FTexture2DMipMap& MipWeight = OutputResolveWeight->GetPlatformData()->Mips[0];
	uint16* MapData = static_cast<uint16*>(MipMap.BulkData.Lock(LOCK_READ_WRITE));
	uint16* WeightData = static_cast<uint16*>(MipWeight.BulkData.Lock(LOCK_READ_WRITE));
	if (!MapData || !WeightData)
	{
		MipMap.BulkData.Unlock();
		MipWeight.BulkData.Unlock();
		return;
	}

	const float AtlasW = static_cast<float>(AtlasSize.X);
	const float AtlasH = static_cast<float>(AtlasSize.Y);

	// Each row is independent. ParallelFor across rows keeps the per-pixel work tight (no thread
	// spawn per pixel) while letting reconfigures of large images stay cheap.
	ParallelFor(SizeXY.Y, [&](int32 Y)
	{
		uint16* MapRow = &MapData[Y * SizeXY.X * 4];
		uint16* WeightRow = &WeightData[Y * SizeXY.X];
		for (int32 X = 0; X < SizeXY.X; ++X)
		{
			// 1) Owning tile by centerline pick: the unique active tile whose owned rect contains P.
			const FTileGeom* Owner = nullptr;
			for (const FTileGeom& T : ActiveTiles)
			{
				if (X >= T.Owned.Min.X && X < T.Owned.Max.X && Y >= T.Owned.Min.Y && Y < T.Owned.Max.Y)
				{
					Owner = &T;
					break;
				}
			}

			float UV_A_X = 0.0f, UV_A_Y = 0.0f;
			float UV_B_X = 0.0f, UV_B_Y = 0.0f;
			float Weight = 1.0f;

			if (Owner)
			{
				// 2) UV_A: the owner's atlas pixel for P. (Atlas tile rect translates output coords
				//    by AtlasOffset - Covered.Min.)
				const float AtlasPixA_X = static_cast<float>(Owner->AtlasOffset.X + (X - Owner->Covered.Min.X)) + 0.5f;
				const float AtlasPixA_Y = static_cast<float>(Owner->AtlasOffset.Y + (Y - Owner->Covered.Min.Y)) + 0.5f;
				UV_A_X = AtlasPixA_X / AtlasW;
				UV_A_Y = AtlasPixA_Y / AtlasH;

				// 3) Nearest seam neighbor for the secondary (B) tap. Skipped entirely when F=0, in
				//    which case the resolve map degrades to (UV_A == UV_B, Weight=1) and the stitch
				//    is a 1:1 atlas copy.
				if (F > 0)
				{
					int32 BestDistance = INT32_MAX;
					const FTileGeom* Neighbor = nullptr;

					// Probe just outside each owned-rect edge to find the neighbor across it. We
					// only test edges that face into the image (an edge flush with the output
					// boundary has no neighbor; its distance is irrelevant).
					auto ProbeEdge = [&](int32 Dist, int32 ProbeX, int32 ProbeY, bool bEdgeHasNeighbor)
					{
						if (!bEdgeHasNeighbor || Dist < 0 || Dist >= F)
						{
							return;
						}
						for (const FTileGeom& T : ActiveTiles)
						{
							if (&T == Owner)
							{
								continue;
							}
							if (ProbeX >= T.Owned.Min.X && ProbeX < T.Owned.Max.X
								&& ProbeY >= T.Owned.Min.Y && ProbeY < T.Owned.Max.Y)
							{
								if (Dist < BestDistance)
								{
									BestDistance = Dist;
									Neighbor = &T;
								}
								break;
							}
						}
					};

					ProbeEdge(X - Owner->Owned.Min.X,         Owner->Owned.Min.X - 1, Y, Owner->Owned.Min.X > 0);
					ProbeEdge(Owner->Owned.Max.X - 1 - X,     Owner->Owned.Max.X,     Y, Owner->Owned.Max.X < SizeXY.X);
					ProbeEdge(Y - Owner->Owned.Min.Y,         X, Owner->Owned.Min.Y - 1, Owner->Owned.Min.Y > 0);
					ProbeEdge(Owner->Owned.Max.Y - 1 - Y,     X, Owner->Owned.Max.Y,     Owner->Owned.Max.Y < SizeXY.Y);

					if (Neighbor)
					{
						const float AtlasPixB_X = static_cast<float>(Neighbor->AtlasOffset.X + (X - Neighbor->Covered.Min.X)) + 0.5f;
						const float AtlasPixB_Y = static_cast<float>(Neighbor->AtlasOffset.Y + (Y - Neighbor->Covered.Min.Y)) + 0.5f;
						UV_B_X = AtlasPixB_X / AtlasW;
						UV_B_Y = AtlasPixB_Y / AtlasH;
						// Weight = 0.5 at the seam centerline (BestDistance=0), ramping linearly to
						// 1.0 at the inner edge of the feather band (BestDistance=F-1, treated as F).
						const float DistF = static_cast<float>(BestDistance);
						const float FF = static_cast<float>(F);
						Weight = FMath::Clamp(0.5f + 0.5f * DistF / FF, 0.5f, 1.0f);
					}
					else
					{
						UV_B_X = UV_A_X;
						UV_B_Y = UV_A_Y;
						Weight = 1.0f;
					}
				}
				else
				{
					UV_B_X = UV_A_X;
					UV_B_Y = UV_A_Y;
					Weight = 1.0f;
				}
			}

			MapRow[X * 4 + 0] = FFloat16(UV_A_X).Encoded;
			MapRow[X * 4 + 1] = FFloat16(UV_A_Y).Encoded;
			MapRow[X * 4 + 2] = FFloat16(UV_B_X).Encoded;
			MapRow[X * 4 + 3] = FFloat16(UV_B_Y).Encoded;
			WeightRow[X] = FFloat16(Weight).Encoded;
		}
	});

	MipMap.BulkData.Unlock();
	MipWeight.BulkData.Unlock();
	OutputResolveMap->UpdateResource();
	OutputResolveWeight->UpdateResource();
}

void UTempoCamera::SetTileDepthEnabled(FTempoCameraTile& Tile, bool bTileDepthEnabled)
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	UMaterialInterface* TargetMat = nullptr;
	if (bTileDepthEnabled)
	{
		TargetMat = TempoSensorsSettings->GetCameraPostProcessMaterialWithDepth().Get();
		if (!TargetMat)
		{
			UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialWithDepth is not set in TempoSensors settings"));
			return;
		}
	}
	else
	{
		TargetMat = TempoSensorsSettings->GetCameraPostProcessMaterialNoDepth().Get();
		if (!TargetMat)
		{
			UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialNoDepth is not set in TempoSensors settings"));
			return;
		}
	}

	// Reuse the existing MID iff its parent matches — avoids creating garbage when nothing
	// material-identity-wise has changed. If the parent differs (depth toggle), retire the old MID
	// into a retention list so it isn't GC'd while render commands still reference it.
	if (!Tile.PostProcessMaterialInstance || Tile.PostProcessMaterialInstance->Parent != TargetMat)
	{
		RetirePPM(Tile.PostProcessMaterialInstance);
		Tile.PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(TargetMat, this);
		UTempoSceneCaptureComponent2D::ApplyDistortionMapToMaterial(Tile.PostProcessMaterialInstance, Tile.DistortionMap);
	}

	if (bTileDepthEnabled)
	{
		Tile.MinDepth = GEngine->NearClipPlane;
		Tile.MaxDepth = TempoSensorsSettings->GetMaxCameraDepth();
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), Tile.MinDepth);
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), Tile.MaxDepth);
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempoCamera_Max_Discrete_Depth);

		// F-Theta (BrownConrady, Rational): single-tile, tile axis = camera axis, so SceneDepth is
		// already depth along the camera axis — the historical contract. Equidistant family
		// (KannalaBrandt, DoubleSphere): emit Euclidean distance from the camera
		// origin instead. Radial distance is invariant under per-tile rotation about the shared
		// origin, which also removes the depth discontinuity at tile seams.
		const bool bRadial =
			LensParameters.LensModel == ETempoLensModel::KannalaBrandt ||
			LensParameters.LensModel == ETempoLensModel::DoubleSphere;
		Tile.PostProcessMaterialInstance->SetScalarParameterValue(TEXT("UseRadialDistance"), bRadial ? 1.0f : 0.0f);
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
	// the post-stitch HDR target, and applying it inside a tile would feed last-frame's stitched
	// equidistant output back into the tile's scene color), then append the distortion PPM.
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

void UTempoCamera::ConfigureTile(FTempoCameraTile& Tile, double YawOffset, double PitchOffset, double FOutput, const FIntPoint& TileSizeXY, const FIntPoint& TileDestOffset, const FIntPoint& OwnedOffset, const FIntPoint& OwnedSize, double AxisShiftXRd, double AxisShiftYRd, bool bActivate)
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
	Tile.OwnedOutputOffset = OwnedOffset;
	Tile.OwnedOutputSize = OwnedSize;
	Tile.OutputFocalLength = FOutput;
	Tile.AxisShiftXRd = AxisShiftXRd;
	Tile.AxisShiftYRd = AxisShiftYRd;
	Tile.SizeXY = TileSizeXY;                 // InitTileDistortionMap will overwrite
	Tile.FOVAngle = 0.0f;                     // InitTileDistortionMap will overwrite

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

	FTempoCameraTile& TL = Tiles[0];
	FTempoCameraTile& TR = Tiles[1];
	FTempoCameraTile& BL = Tiles[2];
	FTempoCameraTile& BR = Tiles[3];

	auto Deactivate = [this](FTempoCameraTile& Tile)
	{
		ConfigureTile(Tile, 0, 0, 0, FIntPoint::ZeroValue, FIntPoint::ZeroValue, FIntPoint::ZeroValue, FIntPoint::ZeroValue, 0.0, 0.0, /*bActivate=*/ false);
	};

	// Build a "no-rotation" model used purely for full-image geometry queries (FOutput,
	// pixel-offset → yaw/pitch). The per-tile model used in InitTileDistortionMap is constructed
	// separately with the tile's actual rotation.
	TUniquePtr<FLensModel> Model = CreateLensModel(LensParameters, 0.0, 0.0);
	const double FOutput = Model->ComputeFOutputForFullImage(SizeXY, FOVAngle);

	const bool bSingleCapture = (LensParameters.LensModel == ETempoLensModel::Pinhole)
		|| (LensParameters.LensModel == ETempoLensModel::BrownConrady)
		|| (LensParameters.LensModel == ETempoLensModel::Rational);

	if (bSingleCapture)
	{
		// Single capture: use TL, deactivate others. No seams, no feather. Radial models are
		// symmetric around the optical axis — no re-aim shift.
		ConfigureTile(TL, 0.0, 0.0, FOutput, SizeXY, FIntPoint::ZeroValue, FIntPoint::ZeroValue, SizeXY, 0.0, 0.0, /*bActivate=*/ true);
		Deactivate(TR);
		Deactivate(BL);
		Deactivate(BR);
		AtlasSize = SizeXY;
	}
	else // Fisheye (Kannala-Brandt or Double Sphere) — multi-tile permitted
	{
		const double VerticalFOV = FOVAngle * static_cast<double>(SizeXY.Y) / static_cast<double>(SizeXY.X);
		const bool bSplitHorizontal = FOVAngle > MaxPerspectiveFOVPerCapture;
		const bool bSplitVertical = VerticalFOV > MaxPerspectiveFOVPerCapture;

		// Feather is only meaningful where two tiles share an edge. With a single tile (no splits)
		// there are no seams, so F=0. Otherwise use the configured value, clamped to keep tiles sane.
		const int32 F = (bSplitHorizontal || bSplitVertical) ? FMath::Max(0, FeatherPixels) : 0;

		// Convert a SIGNED 2D pixel offset (from the full image's optical center) to (yaw, pitch)
		// degrees, via the model. Joint conversion is required for diagonal (4-tile) cases — the
		// 3D direction implied by independent 1D yaw/pitch differs from the 3D direction whose 2D
		// forward projection lands on (Dx, Dy), introducing multi-degree gaps at diagonal seams.
		const auto YawPitchDegFromPixelOffset = [&](double Dx, double Dy)
		{
			double Yaw = 0.0;
			double Pitch = 0.0;
			Model->PixelOffsetToYawPitchDeg(Dx, Dy, FOutput, Yaw, Pitch);
			return TPair<double, double>(Yaw, Pitch);
		};

		// Compute the angular-centroid aim point for a tile defined by its covered-rect corner
		// pixel offsets (relative to the parent image center) and pixel-rect-center pixel offset.
		// Returns (YawDeg, PitchDeg, AxisShiftXRd, AxisShiftYRd):
		//   - (Yaw, Pitch) is the midpoint of the tile's (yaw, pitch) angular extent over the four
		//     covered corners — typically a better optical-axis aim than the pixel-rect center
		//     because the unprojection from pixel-offset to angle is non-linear, so the angular
		//     midpoint and the pixel-offset midpoint differ for off-axis tiles.
		//   - AxisShift is the resulting axis position, expressed as a displacement from the tile's
		//     pixel-rect center in parent r_d units. The distortion model uses this to map
		//     tile-pixel-centered output coords to the right parent-frame r_d when the axis no
		//     longer lands at the tile's pixel center.
		struct FTileAim
		{
			double YawDeg;
			double PitchDeg;
			double AxisShiftXRd;
			double AxisShiftYRd;
		};
		const auto ComputeTileAim = [&](double LDx, double RDx, double TDy, double BDy, double PixelCenterDx, double PixelCenterDy)
		{
			const auto YP_TL = YawPitchDegFromPixelOffset(LDx, TDy);
			const auto YP_TR = YawPitchDegFromPixelOffset(RDx, TDy);
			const auto YP_BL = YawPitchDegFromPixelOffset(LDx, BDy);
			const auto YP_BR = YawPitchDegFromPixelOffset(RDx, BDy);

			const double YawMin = FMath::Min(FMath::Min(YP_TL.Key, YP_TR.Key), FMath::Min(YP_BL.Key, YP_BR.Key));
			const double YawMax = FMath::Max(FMath::Max(YP_TL.Key, YP_TR.Key), FMath::Max(YP_BL.Key, YP_BR.Key));
			const double PitchMin = FMath::Min(FMath::Min(YP_TL.Value, YP_TR.Value), FMath::Min(YP_BL.Value, YP_BR.Value));
			const double PitchMax = FMath::Max(FMath::Max(YP_TL.Value, YP_TR.Value), FMath::Max(YP_BL.Value, YP_BR.Value));

			FTileAim Aim;
			Aim.YawDeg = 0.5 * (YawMin + YawMax);
			Aim.PitchDeg = 0.5 * (PitchMin + PitchMax);

			// Forward-project the centroid back to a pixel offset to recover the axis position
			// in the parent image plane, then express its displacement from the tile's pixel
			// center in r_d units (FOutput is pixels/r_d for the model's output unit).
			double DxAxis = 0.0;
			double DyAxis = 0.0;
			Model->YawPitchDegToPixelOffset(Aim.YawDeg, Aim.PitchDeg, FOutput, DxAxis, DyAxis);
			Aim.AxisShiftXRd = (DxAxis - PixelCenterDx) / FOutput;
			Aim.AxisShiftYRd = (DyAxis - PixelCenterDy) / FOutput;
			return Aim;
		};

		if (!bSplitHorizontal && !bSplitVertical)
		{
			// Single fisheye tile centered on the optical axis — angular centroid is exactly (0,0).
			ConfigureTile(TL, 0.0, 0.0, FOutput, SizeXY, FIntPoint::ZeroValue, FIntPoint::ZeroValue, SizeXY, 0.0, 0.0, true);
			Deactivate(TR);
			Deactivate(BL);
			Deactivate(BR);
			AtlasSize = SizeXY;
		}
		else if (bSplitHorizontal && !bSplitVertical)
		{
			// Owned: split exactly into left/right halves; sum equals SizeXY.X.
			const int32 LeftWidth = FMath::CeilToInt32(SizeXY.X / 2.0);
			const int32 RightWidth = SizeXY.X - LeftWidth;

			// Covered: each tile extends F pixels into the other's owned region across the seam.
			const int32 TL_CoveredW = LeftWidth + F;
			const int32 TR_CoveredW = RightWidth + F;

			const double HalfX = SizeXY.X / 2.0;
			const double HalfY = SizeXY.Y / 2.0;

			// Tile pixel-rect centers (parent-frame pixel offsets).
			const double TL_PCDx = TL_CoveredW * 0.5 - HalfX;
			const double TR_PCDx = HalfX - TR_CoveredW * 0.5;

			// Tile covered-rect corner pixel offsets (top is -Dy, bottom is +Dy).
			const FTileAim TL_Aim = ComputeTileAim(-HalfX, TL_PCDx + TL_CoveredW * 0.5, -HalfY, +HalfY, TL_PCDx, 0.0);
			const FTileAim TR_Aim = ComputeTileAim(TR_PCDx - TR_CoveredW * 0.5, +HalfX, -HalfY, +HalfY, TR_PCDx, 0.0);

			ConfigureTile(TL, TL_Aim.YawDeg, TL_Aim.PitchDeg, FOutput, FIntPoint(TL_CoveredW, SizeXY.Y), FIntPoint(0, 0),
				FIntPoint(0, 0), FIntPoint(LeftWidth, SizeXY.Y), TL_Aim.AxisShiftXRd, TL_Aim.AxisShiftYRd, true);
			ConfigureTile(TR, TR_Aim.YawDeg, TR_Aim.PitchDeg, FOutput, FIntPoint(TR_CoveredW, SizeXY.Y), FIntPoint(TL_CoveredW, 0),
				FIntPoint(LeftWidth, 0), FIntPoint(RightWidth, SizeXY.Y), TR_Aim.AxisShiftXRd, TR_Aim.AxisShiftYRd, true);
			Deactivate(BL);
			Deactivate(BR);
			AtlasSize = FIntPoint(TL_CoveredW + TR_CoveredW, SizeXY.Y);
		}
		else if (!bSplitHorizontal && bSplitVertical)
		{
			const int32 TopHeight = FMath::CeilToInt32(SizeXY.Y / 2.0);
			const int32 BottomHeight = SizeXY.Y - TopHeight;

			const int32 Top_CoveredH = TopHeight + F;
			const int32 Bottom_CoveredH = BottomHeight + F;

			const double HalfX = SizeXY.X / 2.0;
			const double HalfY = SizeXY.Y / 2.0;

			const double Top_PCDy = Top_CoveredH * 0.5 - HalfY;
			const double Bottom_PCDy = HalfY - Bottom_CoveredH * 0.5;

			const FTileAim Top_Aim = ComputeTileAim(-HalfX, +HalfX, -HalfY, Top_PCDy + Top_CoveredH * 0.5, 0.0, Top_PCDy);
			const FTileAim Bottom_Aim = ComputeTileAim(-HalfX, +HalfX, Bottom_PCDy - Bottom_CoveredH * 0.5, +HalfY, 0.0, Bottom_PCDy);

			ConfigureTile(TL, Top_Aim.YawDeg, Top_Aim.PitchDeg, FOutput, FIntPoint(SizeXY.X, Top_CoveredH), FIntPoint(0, 0),
				FIntPoint(0, 0), FIntPoint(SizeXY.X, TopHeight), Top_Aim.AxisShiftXRd, Top_Aim.AxisShiftYRd, true);
			ConfigureTile(TR, Bottom_Aim.YawDeg, Bottom_Aim.PitchDeg, FOutput, FIntPoint(SizeXY.X, Bottom_CoveredH), FIntPoint(0, Top_CoveredH),
				FIntPoint(0, TopHeight), FIntPoint(SizeXY.X, BottomHeight), Bottom_Aim.AxisShiftXRd, Bottom_Aim.AxisShiftYRd, true);
			Deactivate(BL);
			Deactivate(BR);
			AtlasSize = FIntPoint(SizeXY.X, Top_CoveredH + Bottom_CoveredH);
		}
		else
		{
			const int32 LeftWidth = FMath::CeilToInt32(SizeXY.X / 2.0);
			const int32 RightWidth = SizeXY.X - LeftWidth;
			const int32 TopHeight = FMath::CeilToInt32(SizeXY.Y / 2.0);
			const int32 BottomHeight = SizeXY.Y - TopHeight;

			const int32 L_CoveredW = LeftWidth + F;
			const int32 R_CoveredW = RightWidth + F;
			const int32 T_CoveredH = TopHeight + F;
			const int32 B_CoveredH = BottomHeight + F;

			const double HalfX = SizeXY.X / 2.0;
			const double HalfY = SizeXY.Y / 2.0;

			// Pixel-rect centers per quadrant.
			const double L_PCDx = L_CoveredW * 0.5 - HalfX;
			const double R_PCDx = HalfX - R_CoveredW * 0.5;
			const double T_PCDy = T_CoveredH * 0.5 - HalfY;
			const double B_PCDy = HalfY - B_CoveredH * 0.5;

			// Covered-rect corner X/Y for each tile (parent-frame pixel offsets).
			const double LCornerInner = L_PCDx + L_CoveredW * 0.5;     // shared seam x for TL/BL
			const double RCornerInner = R_PCDx - R_CoveredW * 0.5;     // shared seam x for TR/BR
			const double TCornerInner = T_PCDy + T_CoveredH * 0.5;     // shared seam y for TL/TR
			const double BCornerInner = B_PCDy - B_CoveredH * 0.5;     // shared seam y for BL/BR

			const FTileAim TL_Aim = ComputeTileAim(-HalfX, LCornerInner, -HalfY, TCornerInner, L_PCDx, T_PCDy);
			const FTileAim TR_Aim = ComputeTileAim(RCornerInner, +HalfX, -HalfY, TCornerInner, R_PCDx, T_PCDy);
			const FTileAim BL_Aim = ComputeTileAim(-HalfX, LCornerInner, BCornerInner, +HalfY, L_PCDx, B_PCDy);
			const FTileAim BR_Aim = ComputeTileAim(RCornerInner, +HalfX, BCornerInner, +HalfY, R_PCDx, B_PCDy);

			ConfigureTile(TL, TL_Aim.YawDeg, TL_Aim.PitchDeg, FOutput, FIntPoint(L_CoveredW, T_CoveredH), FIntPoint(0, 0),
				FIntPoint(0, 0), FIntPoint(LeftWidth, TopHeight), TL_Aim.AxisShiftXRd, TL_Aim.AxisShiftYRd, true);
			ConfigureTile(TR, TR_Aim.YawDeg, TR_Aim.PitchDeg, FOutput, FIntPoint(R_CoveredW, T_CoveredH), FIntPoint(L_CoveredW, 0),
				FIntPoint(LeftWidth, 0), FIntPoint(RightWidth, TopHeight), TR_Aim.AxisShiftXRd, TR_Aim.AxisShiftYRd, true);
			ConfigureTile(BL, BL_Aim.YawDeg, BL_Aim.PitchDeg, FOutput, FIntPoint(L_CoveredW, B_CoveredH), FIntPoint(0, T_CoveredH),
				FIntPoint(0, TopHeight), FIntPoint(LeftWidth, BottomHeight), BL_Aim.AxisShiftXRd, BL_Aim.AxisShiftYRd, true);
			ConfigureTile(BR, BR_Aim.YawDeg, BR_Aim.PitchDeg, FOutput, FIntPoint(R_CoveredW, B_CoveredH), FIntPoint(L_CoveredW, T_CoveredH),
				FIntPoint(LeftWidth, TopHeight), FIntPoint(RightWidth, BottomHeight), BR_Aim.AxisShiftXRd, BR_Aim.AxisShiftYRd, true);
			AtlasSize = FIntPoint(L_CoveredW + R_CoveredW, T_CoveredH + B_CoveredH);
		}
	}

	RebuildResolveMaps();
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
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, FeatherPixels) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, bAutoTextureFilterType) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, TextureFilterType) ||
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
