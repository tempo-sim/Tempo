// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"

#include "TempoActorLabeler.h"
#include "TempoCameraModule.h"
#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"
#include "TempoLabelTypes.h"
#include "TempoSensorsConstants.h"
#include "TempoSensorsSettings.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Box2D.h"
#include "TextureResource.h"

namespace
{
	const FName TLCaptureComponentTag(TEXT("_TL"));
	const FName TRCaptureComponentTag(TEXT("_TR"));
	const FName BLCaptureComponentTag(TEXT("_BL"));
	const FName BRCaptureComponentTag(TEXT("_BR"));

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
			DepthImage.set_depths(Idx, Image[Idx].Depth(MinDepth, MaxDepth, GTempo_Max_Discrete_Depth));
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
// UTempoCameraCaptureComponent
// ------------------------------------------------------------------------------------

UTempoCameraCaptureComponent::UTempoCameraCaptureComponent()
{
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// Start with no-depth settings
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	PixelFormatOverride = EPixelFormat::PF_Unknown;

	bAutoActivate = false;
}

void UTempoCameraCaptureComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	if (ProxyMeshComponent)
	{
		ProxyMeshComponent->SetVisibility(false);
	}
#endif
}

void UTempoCameraCaptureComponent::ApplyRenderSettings()
{
	if (!CameraOwner)
	{
		return;
	}

	PostProcessSettings = CameraOwner->PostProcessSettings;

	// Re-append the distortion/label post-process material if it was already created
	// (on first call from Configure it is still null and will be added by SetDepthEnabled).
	if (PostProcessMaterialInstance)
	{
		PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0, PostProcessMaterialInstance));
	}

	SetShowFlagSettings(CameraOwner->ShowFlagSettings);

	bUseRayTracingIfEnabled = CameraOwner->bUseRayTracingIfEnabled;
}

void UTempoCameraCaptureComponent::Activate(bool bReset)
{
	// Set up depth format and materials BEFORE Super::Activate, which calls InitRenderTarget.
	// This ensures InitRenderTarget creates the render target with the correct format on the first call,
	// avoiding a double-init race condition where two render commands compete over StagingTextures.
	SetDepthEnabled(CameraOwner->bDepthEnabled);

	Super::Activate(bReset);
}

void UTempoCameraCaptureComponent::SetDepthEnabled(bool bDepthEnabled)
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	if (bDepthEnabled)
	{
		RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		PixelFormatOverride = EPixelFormat::PF_A16B16G16R16;

		if (const TObjectPtr<UMaterialInterface> PostProcessMaterialWithDepth = TempoSensorsSettings->GetCameraPostProcessMaterialWithDepth())
		{
			PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterialWithDepth.Get(), this);
			ApplyDistortionMapToMaterial(PostProcessMaterialInstance);
			MinDepth = GEngine->NearClipPlane;
			MaxDepth = TempoSensorsSettings->GetMaxCameraDepth();
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), MinDepth);
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), MaxDepth);
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempo_Max_Discrete_Depth);
		}
		else
		{
			UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialWithDepth is not set in TempoSensors settings"));
		}
	}
	else
	{
		if (const TObjectPtr<UMaterialInterface> PostProcessMaterialNoDepth = TempoSensorsSettings->GetCameraPostProcessMaterialNoDepth())
		{
			PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterialNoDepth.Get(), this);
			ApplyDistortionMapToMaterial(PostProcessMaterialInstance);
		}
		else
		{
			UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialNoDepth is not set in TempoSensors settings"));
		}

		RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		PixelFormatOverride = EPixelFormat::PF_Unknown;
	}

	// Set up label override parameters
	UDataTable* SemanticLabelTable = GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable();
	FName OverridableLabelRowName = TempoSensorsSettings->GetOverridableLabelRowName();
	FName OverridingLabelRowName = TempoSensorsSettings->GetOverridingLabelRowName();
	TOptional<int32> OverridableLabel;
	TOptional<int32> OverridingLabel;

	if (!OverridableLabelRowName.IsNone())
	{
		SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""),
			[&OverridableLabelRowName,
			 &OverridingLabelRowName,
			 &OverridableLabel,
			 &OverridingLabel](const FName& Key, const FSemanticLabel& Value)
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

	if (PostProcessMaterialInstance)
	{
		if (OverridableLabel.IsSet() && OverridingLabel.IsSet())
		{
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridableLabel"), OverridableLabel.GetValue());
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), OverridingLabel.GetValue());
		}
		else
		{
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), 0.0);
		}
		// Rebuild blendables from the owner's user-authored list, then append the distortion/label material.
		PostProcessSettings.WeightedBlendables.Array.Empty();
		if (CameraOwner)
		{
			PostProcessSettings.WeightedBlendables.Array.Append(CameraOwner->PostProcessSettings.WeightedBlendables.Array);
		}
		PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0, PostProcessMaterialInstance));
		PostProcessMaterialInstance->EnsureIsComplete();
	}
	else
	{
		UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialInstance is not set."));
	}
}

bool UTempoCameraCaptureComponent::HasPendingRequests() const
{
	return CameraOwner->HasPendingCameraRequests();
}

FTextureRead* UTempoCameraCaptureComponent::MakeTextureRead() const
{
	check(GetWorld());

	TMap<uint8, uint8> InstanceToSemanticMap;
	if (UTempoActorLabeler* Labeler = GetWorld()->GetSubsystem<UTempoActorLabeler>())
	{
		InstanceToSemanticMap = Labeler->GetInstanceToSemanticIdMap();
	}

	return CameraOwner->bDepthEnabled ?
		static_cast<FTextureRead*>(new TTextureRead<FCameraPixelWithDepth>(SizeXY, CameraOwner->SequenceId + NumPendingTextureReads(), GetWorld()->GetTimeSeconds(), CameraOwner->GetOwnerName(), CameraOwner->GetSensorName(), CameraOwner->GetComponentTransform(), MinDepth, MaxDepth, MoveTemp(InstanceToSemanticMap))) :
		static_cast<FTextureRead*>(new TTextureRead<FCameraPixelNoDepth>(SizeXY, CameraOwner->SequenceId + NumPendingTextureReads(), GetWorld()->GetTimeSeconds(), CameraOwner->GetOwnerName(), CameraOwner->GetSensorName(), CameraOwner->GetComponentTransform(), MoveTemp(InstanceToSemanticMap)));
}

int32 UTempoCameraCaptureComponent::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
}

void UTempoCameraCaptureComponent::Configure(double YawOffset, double PitchOffset, double EquidistantTileFOV, const FIntPoint& InTileSizeXY, const FIntPoint& InTileDestOffset)
{
	RateHz = CameraOwner->RateHz;
	TileOutputSizeXY = InTileSizeXY;
	TileDestOffset = InTileDestOffset;
	TileOutputOffsetY = 0;
	EquidistantTileHFOVRad = FMath::DegreesToRadians(EquidistantTileFOV);
	SetRelativeRotation(FRotator(PitchOffset, YawOffset, 0.0));

	// SizeXY and FOVAngle are set in InitDistortionMap via ComputeRenderConfig.
	// Set SizeXY to the output size initially; InitDistortionMap may adjust it.
	SizeXY = InTileSizeXY;

	ApplyRenderSettings();
}

void UTempoCameraCaptureComponent::InitDistortionMap()
{
	if (TileOutputSizeXY.X <= 0 || TileOutputSizeXY.Y <= 0)
	{
		return;
	}

	TUniquePtr<FDistortionModel> Model = CreateDistortionModel(
		CameraOwner->LensParameters,
		GetRelativeRotation().Yaw,
		GetRelativeRotation().Pitch);
	const float OutputHFOV = FMath::RadiansToDegrees(EquidistantTileHFOVRad);
	const FDistortionRenderConfig Config = Model->ComputeRenderConfig(TileOutputSizeXY, OutputHFOV);

	// Apply render configuration to the scene capture.
	FOVAngle = Config.RenderFOVAngle;
	SizeXY = Config.RenderSizeXY;

	// Create the distortion map at output resolution and fill it.
	CreateOrResizeDistortionMapTexture(TileOutputSizeXY);
	FillDistortionMap(*Model, TileOutputSizeXY, Config.FOutput, Config.RenderSizeXY, Config.FRender);

	// Wire the distortion map to the post-process material (must happen after texture creation).
	ApplyDistortionMapToMaterial(PostProcessMaterialInstance);
}

// ------------------------------------------------------------------------------------
// UTempoCamera
// ------------------------------------------------------------------------------------

UTempoCamera::UTempoCamera()
{
	PrimaryComponentTick.bCanEverTick = true;
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE, EMeasurementType::BOUNDING_BOXES };
	bAutoActivate = true;

	// Defaults propagated to the managed capture components.
	// Manual exposure by default so tiles render with a consistent exposure
	// (per-tile auto exposure would diverge across independent ViewStates).
	PostProcessSettings.bOverride_AutoExposureMethod = true;
	PostProcessSettings.AutoExposureMethod = AEM_Manual;
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

	ShowFlagSettings.Add({ TEXT("AntiAliasing"), true });
	ShowFlagSettings.Add({ TEXT("TemporalAA"), true });
	ShowFlagSettings.Add({ TEXT("MotionBlur"), false });

}

void UTempoCamera::OnRegister()
{
	Super::OnRegister();

	// Don't create capture components during cooking or for template/archetype objects
	// (e.g. Blueprint editor previews where GetOwner() is not a properly-packaged actor).
	if (IsRunningCommandlet() || IsTemplate())
	{
		return;
	}

	SyncCaptureComponents();
	UpdateInternalMirrors();

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		// Activate() runs when added to a live world, but may be skipped in some registration
		// paths. Init the shared RT here so it's ready for the first capture regardless.
		InitSharedRenderTarget();
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
		|| HorizontalFOV != HorizontalFOV_Internal
		|| LensParameters != LensParameters_Internal;
}

bool UTempoCamera::AnyCaptureReadsInFlight() const
{
	for (const UTempoCameraCaptureComponent* Component : GetActiveCaptureComponents())
	{
		if (Component->NumPendingTextureReads() > 0)
		{
			return true;
		}
	}
	return false;
}

void UTempoCamera::TryApplyPendingReconfigure()
{
	// Don't create capture components during cooking or for template/archetype objects
	// (e.g. Blueprint editor previews where GetOwner() is not a properly-packaged actor).
	if (IsRunningCommandlet() || IsTemplate())
	{
		return;
	}
	if (!bReconfigurePending)
	{
		return;
	}
	if (AnyCaptureReadsInFlight())
	{
		return;
	}
	ReconfigureCaptureComponentsNow();
	bReconfigurePending = false;
}

void UTempoCamera::ReconfigureCaptureComponentsNow()
{
	// Drain active tiles first so SyncCaptureComponents' re-Activate rebuilds render targets
	// and distortion maps with the new parameters. Deactivate() empties the texture read queue,
	// which is safe because callers gate this on AnyCaptureReadsInFlight() == false.
	for (UTempoCameraCaptureComponent* Component : GetActiveCaptureComponents())
	{
		Component->Deactivate();
	}

	SyncCaptureComponents();
	UpdateInternalMirrors();

	// The shared RT needs to resize when SizeXY changed, and any queued reads reference the
	// previous RT geometry so are no longer valid.
	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitSharedRenderTarget();
	}
}

void UTempoCamera::UpdateInternalMirrors()
{
	LensParameters_Internal = LensParameters;
	HorizontalFOV_Internal = HorizontalFOV;
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
	if (!TextureReadQueue.IsNextAwaitingRender() || !SharedTextureTarget)
	{
		return;
	}

	const FRenderTarget* RenderTarget = SharedTextureTarget->GetRenderTargetResource();
	if (!ensureMsgf(RenderTarget, TEXT("SharedTextureTarget was not initialized. Skipping texture read.")))
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

	if (TextureReadQueue.NextReadComplete())
	{
		TUniquePtr<FTextureRead> TextureRead = TextureReadQueue.DequeueIfReadComplete();
		Future = DecodeAndRespond(MoveTemp(TextureRead));

		PendingColorImageRequests.Empty();
		PendingLabelImageRequests.Empty();
		PendingBoundingBoxesRequests.Empty();
		if (bDepthEnabled)
		{
			PendingDepthImageRequests.Empty();
		}
	}

	// Toggle depth based on current pending requests — takes effect on the next MaybeCapture.
	if (!bDepthEnabled && !PendingDepthImageRequests.IsEmpty())
	{
		SetDepthEnabled(true);
	}
	if (bDepthEnabled && PendingDepthImageRequests.IsEmpty())
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
	return FTempoCameraIntrinsics(SizeXY, HorizontalFOV);
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
	Super::Activate(bReset);

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitSharedRenderTarget();
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

void UTempoCamera::InitSharedRenderTarget()
{
	if (SizeXY.X <= 0 || SizeXY.Y <= 0)
	{
		return;
	}

	// Wait for any previous staging texture init render command to complete before modifying
	// StagingTextures, since the render command accesses the array via raw pointer.
	TextureInitFence.Wait();

	SharedTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedTextureTarget->TargetGamma = GetDefault<UTempoSensorsSettings>()->GetSceneCaptureGamma();
	SharedTextureTarget->bGPUSharedFlag = true;

	const ETextureRenderTargetFormat Format = bDepthEnabled
		? ETextureRenderTargetFormat::RTF_RGBA16f
		: ETextureRenderTargetFormat::RTF_RGBA8;
	const EPixelFormat PixelFormatOverride = bDepthEnabled
		? EPixelFormat::PF_A16B16G16R16
		: EPixelFormat::PF_Unknown;

	SharedTextureTarget->RenderTargetFormat = Format;
	if (PixelFormatOverride == EPixelFormat::PF_Unknown)
	{
		SharedTextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);
	}
	else
	{
		SharedTextureTarget->InitCustomFormat(SizeXY.X, SizeXY.Y, PixelFormatOverride, true);
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
		SharedTextureTarget->SizeX,
		SharedTextureTarget->SizeY,
		SharedTextureTarget->GetFormat(),
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

namespace
{
	struct FTileCopyInfo
	{
		FTextureRenderTargetResource* TileRT;
		FIntPoint TileDestOffset;
		FIntPoint TileOutputSizeXY;
		int32 TileOutputOffsetY;
	};
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

	const TArray<UTempoCameraCaptureComponent*> ActiveCaptureComponents = GetActiveCaptureComponents();
	if (ActiveCaptureComponents.IsEmpty())
	{
		return;
	}

	const int32 MaxQueueSize = GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
	if (MaxQueueSize > 0 && TextureReadQueue.Num() > MaxQueueSize)
	{
		UE_LOG(LogTempoCamera, Warning, TEXT("Fell behind while reading frames from sensor %s. Skipping capture."), *GetName());
		return;
	}

	// Require that each active tile has a valid render target of the expected format.
	for (const UTempoCameraCaptureComponent* Tile : ActiveCaptureComponents)
	{
		if (!Tile->TextureTarget || !Tile->TextureTarget->GameThread_GetRenderTargetResource() ||
			Tile->TextureTarget->GetFormat() != SharedTextureTarget->GetFormat())
		{
			return;
		}
	}

	FTextureRenderTargetResource* SharedRTResource = SharedTextureTarget->GameThread_GetRenderTargetResource();
	if (!SharedRTResource)
	{
		return;
	}

	// Capture each tile. With CaptureScene() (not CaptureSceneDeferred) the tile's render commands
	// enqueue immediately in FIFO order, so the stitch command we enqueue right after will run
	// after all tile renders on the render thread.
	for (UTempoCameraCaptureComponent* Tile : ActiveCaptureComponents)
	{
		Tile->CaptureScene();
	}

	// Gather copy info from each active tile.
	TArray<FTileCopyInfo> TileInfos;
	TileInfos.Reserve(ActiveCaptureComponents.Num());
	for (UTempoCameraCaptureComponent* Tile : ActiveCaptureComponents)
	{
		FTileCopyInfo Info;
		Info.TileRT = Tile->TextureTarget->GameThread_GetRenderTargetResource();
		Info.TileDestOffset = Tile->TileDestOffset;
		Info.TileOutputSizeXY = Tile->TileOutputSizeXY;
		Info.TileOutputOffsetY = Tile->TileOutputOffsetY;
		TileInfos.Add(Info);
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

	ENQUEUE_RENDER_COMMAND(TempoCameraStitchAndReadback)(
		[SharedRTResource, StagingTex, TileInfos, NewRead](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SharedRT = SharedRTResource->GetRenderTargetTexture();

			// Transition shared RT to CopyDest, then copy each tile sub-rect into it.
			RHICmdList.Transition(FRHITransitionInfo(SharedRT, ERHIAccess::Unknown, ERHIAccess::CopyDest));

			for (const FTileCopyInfo& Info : TileInfos)
			{
				FRHITexture* TileRT = Info.TileRT->GetRenderTargetTexture();
				RHICmdList.Transition(FRHITransitionInfo(TileRT, ERHIAccess::Unknown, ERHIAccess::CopySrc));
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.SourcePosition = FIntVector(0, Info.TileOutputOffsetY, 0);
				CopyInfo.DestPosition = FIntVector(Info.TileDestOffset.X, Info.TileDestOffset.Y, 0);
				CopyInfo.Size = FIntVector(Info.TileOutputSizeXY.X, Info.TileOutputSizeXY.Y, 1);
				RHICmdList.CopyTexture(TileRT, SharedRT, CopyInfo);
			}

			// Transition shared RT to CopySrc, then copy into staging for CPU readback.
			RHICmdList.Transition(FRHITransitionInfo(SharedRT, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			RHICmdList.CopyTexture(SharedRT, StagingTex, FRHICopyTextureInfo());

			// Fence so the game thread can poll for readback readiness.
			NewRead->RenderFence = RHICreateGPUFence(TEXT("TempoCameraRenderFence"));
			RHICmdList.WriteGPUFence(NewRead->RenderFence);
		});

	TextureReadQueue.Enqueue(NewRead);
}

// ------------------------------------------------------------------------------------
// Capture Component Management
// ------------------------------------------------------------------------------------

TMap<FName, UTempoCameraCaptureComponent*> UTempoCamera::GetAllCaptureComponents() const
{
	TMap<FName, UTempoCameraCaptureComponent*> CaptureComponents;
	TArray<USceneComponent*> ChildrenComponents;
	GetChildrenComponents(false, ChildrenComponents);
	for (USceneComponent* ChildComponent : ChildrenComponents)
	{
		for (const FName& Tag : { TLCaptureComponentTag, TRCaptureComponentTag, BLCaptureComponentTag, BRCaptureComponentTag })
		{
			if (UTempoCameraCaptureComponent* CameraCaptureComponent = Cast<UTempoCameraCaptureComponent>(ChildComponent); ChildComponent->ComponentHasTag(Tag))
			{
				CaptureComponents.Add(Tag, CameraCaptureComponent);
			}
		}
	}
	return CaptureComponents;
}

TMap<FName, UTempoCameraCaptureComponent*> UTempoCamera::GetOrCreateCaptureComponents()
{
	TMap<FName, UTempoCameraCaptureComponent*> CaptureComponents = GetAllCaptureComponents();
	for (const FName& Tag : { TLCaptureComponentTag, TRCaptureComponentTag, BLCaptureComponentTag, BRCaptureComponentTag })
	{
		if (!CaptureComponents.Contains(Tag))
		{
			const FName ComponentName(GetName() + Tag.ToString());
			UTempoCameraCaptureComponent* CaptureComponent = NewObject<UTempoCameraCaptureComponent>(GetOwner(), UTempoCameraCaptureComponent::StaticClass(), ComponentName);
			CaptureComponent->CameraOwner = this;
			CaptureComponent->ComponentTags.AddUnique(Tag);
			CaptureComponent->OnComponentCreated();
			CaptureComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			CaptureComponent->RegisterComponent();
			GetOwner()->AddInstanceComponent(CaptureComponent);
			CaptureComponents.Add(Tag, CaptureComponent);
		}
	}

	return CaptureComponents;
}

TArray<UTempoCameraCaptureComponent*> UTempoCamera::GetActiveCaptureComponents() const
{
	TArray<UTempoCameraCaptureComponent*> ActiveCaptureComponents;
	TMap<FName, UTempoCameraCaptureComponent*> CaptureComponents = GetAllCaptureComponents();
	for (const auto& Elem : CaptureComponents)
	{
		if (Elem.Value->IsActive())
		{
			ActiveCaptureComponents.Add(Elem.Value);
		}
	}
	return ActiveCaptureComponents;
}

static void SyncCaptureComponent(UTempoCameraCaptureComponent* CaptureComponent, bool bActive, double YawOffset, double PitchOffset, double PerspectiveFOV, const FIntPoint& TileSizeXY, const FIntPoint& TileDestOffset = FIntPoint::ZeroValue)
{
	CaptureComponent->Configure(YawOffset, PitchOffset, PerspectiveFOV, TileSizeXY, TileDestOffset);
	if (bActive && !CaptureComponent->IsActive())
	{
		CaptureComponent->Activate();
	}
	if (!bActive && CaptureComponent->IsActive())
	{
		CaptureComponent->Deactivate();
	}
}

void UTempoCamera::ValidateFOV() const
{
	if (LensParameters.DistortionModel == ETempoDistortionModel::BrownConrady || LensParameters.DistortionModel == ETempoDistortionModel::Rational)
	{
		ensureMsgf(HorizontalFOV <= 170.0f, TEXT("%s HorizontalFOV %.2f exceeds max 170 degrees."),
			LensParameters.DistortionModel == ETempoDistortionModel::Rational ? TEXT("Rational") : TEXT("BrownConrady"), HorizontalFOV);
	}
	else
	{
		const double VerticalFOV = HorizontalFOV * static_cast<double>(SizeXY.Y) / static_cast<double>(SizeXY.X);
		ensureMsgf(HorizontalFOV <= 240.0f, TEXT("Equidistant HorizontalFOV %.2f exceeds max 240 degrees."), HorizontalFOV);
		ensureMsgf(VerticalFOV <= 240.0, TEXT("Equidistant VerticalFOV %.2f (derived) exceeds max 240 degrees."), VerticalFOV);
	}
}

void UTempoCamera::SyncCaptureComponents()
{
	ValidateFOV();

	const TMap<FName, UTempoCameraCaptureComponent*> CaptureComponents = GetOrCreateCaptureComponents();

	UTempoCameraCaptureComponent* const* TLCaptureComponent = CaptureComponents.Find(TLCaptureComponentTag);
	UTempoCameraCaptureComponent* const* TRCaptureComponent = CaptureComponents.Find(TRCaptureComponentTag);
	UTempoCameraCaptureComponent* const* BLCaptureComponent = CaptureComponents.Find(BLCaptureComponentTag);
	UTempoCameraCaptureComponent* const* BRCaptureComponent = CaptureComponents.Find(BRCaptureComponentTag);

	if (!TLCaptureComponent || !TRCaptureComponent || !BLCaptureComponent || !BRCaptureComponent)
	{
		return;
	}

	if (LensParameters.DistortionModel == ETempoDistortionModel::BrownConrady || LensParameters.DistortionModel == ETempoDistortionModel::Rational)
	{
		// Single capture: use TL, deactivate others
		SyncCaptureComponent(*TLCaptureComponent, true, 0.0, 0.0, HorizontalFOV, SizeXY);
		SyncCaptureComponent(*TRCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		SyncCaptureComponent(*BLCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		SyncCaptureComponent(*BRCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
	}
	else // Equidistant
	{
		const double VerticalFOV = HorizontalFOV * static_cast<double>(SizeXY.Y) / static_cast<double>(SizeXY.X);
		const bool bSplitHorizontal = HorizontalFOV > MaxPerspectiveFOVPerCapture;
		const bool bSplitVertical = VerticalFOV > MaxPerspectiveFOVPerCapture;

		if (!bSplitHorizontal && !bSplitVertical)
		{
			// 1 capture
			SyncCaptureComponent(*TLCaptureComponent, true, 0.0, 0.0, HorizontalFOV, SizeXY);
			SyncCaptureComponent(*TRCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
			SyncCaptureComponent(*BLCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
			SyncCaptureComponent(*BRCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		}
		else if (bSplitHorizontal && !bSplitVertical)
		{
			// 2 captures: left + right
			const double SubFOV = HorizontalFOV / 2.0;
			const double YawOffset = SubFOV / 2.0;
			const int32 LeftWidth = FMath::CeilToInt32(SizeXY.X / 2.0);
			const int32 RightWidth = SizeXY.X - LeftWidth;

			SyncCaptureComponent(*TLCaptureComponent, true, -YawOffset, 0.0, SubFOV, FIntPoint(LeftWidth, SizeXY.Y), FIntPoint(0, 0));
			SyncCaptureComponent(*TRCaptureComponent, true, YawOffset, 0.0, SubFOV, FIntPoint(RightWidth, SizeXY.Y), FIntPoint(LeftWidth, 0));
			SyncCaptureComponent(*BLCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
			SyncCaptureComponent(*BRCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		}
		else if (!bSplitHorizontal && bSplitVertical)
		{
			// 2 captures: top + bottom
			const double SubVertFOV = VerticalFOV / 2.0;
			const double PitchOffset = SubVertFOV / 2.0;
			const int32 TopHeight = FMath::CeilToInt32(SizeXY.Y / 2.0);
			const int32 BottomHeight = SizeXY.Y - TopHeight;

			SyncCaptureComponent(*TLCaptureComponent, true, 0.0, PitchOffset, HorizontalFOV, FIntPoint(SizeXY.X, TopHeight), FIntPoint(0, 0));
			SyncCaptureComponent(*TRCaptureComponent, true, 0.0, -PitchOffset, HorizontalFOV, FIntPoint(SizeXY.X, BottomHeight), FIntPoint(0, TopHeight));
			SyncCaptureComponent(*BLCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
			SyncCaptureComponent(*BRCaptureComponent, false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		}
		else
		{
			// 4 captures: 2x2 grid
			const double SubHFOV = HorizontalFOV / 2.0;
			const double SubVFOV = VerticalFOV / 2.0;
			const double YawOffset = SubHFOV / 2.0;
			const double PitchOffset = SubVFOV / 2.0;
			const int32 LeftWidth = FMath::CeilToInt32(SizeXY.X / 2.0);
			const int32 RightWidth = SizeXY.X - LeftWidth;
			const int32 TopHeight = FMath::CeilToInt32(SizeXY.Y / 2.0);
			const int32 BottomHeight = SizeXY.Y - TopHeight;

			SyncCaptureComponent(*TLCaptureComponent, true, -YawOffset, PitchOffset, SubHFOV, FIntPoint(LeftWidth, TopHeight), FIntPoint(0, 0));
			SyncCaptureComponent(*TRCaptureComponent, true, YawOffset, PitchOffset, SubHFOV, FIntPoint(RightWidth, TopHeight), FIntPoint(LeftWidth, 0));
			SyncCaptureComponent(*BLCaptureComponent, true, -YawOffset, -PitchOffset, SubHFOV, FIntPoint(LeftWidth, BottomHeight), FIntPoint(0, TopHeight));
			SyncCaptureComponent(*BRCaptureComponent, true, YawOffset, -PitchOffset, SubHFOV, FIntPoint(RightWidth, BottomHeight), FIntPoint(LeftWidth, TopHeight));
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

	// Deactivate and reactivate to reinitialize render targets with the new format.
	// Activate calls SetDepthEnabled (to set format/materials) then Super::Activate (to init render target).
	for (UTempoCameraCaptureComponent* CaptureComponent : GetActiveCaptureComponents())
	{
		CaptureComponent->Deactivate();
		CaptureComponent->Activate();
	}

	// Shared RT format depends on bDepthEnabled, so it must be rebuilt too.
	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitSharedRenderTarget();
	}
}

#if WITH_EDITOR
void UTempoCamera::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, HorizontalFOV) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, SizeXY) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, LensParameters) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, PostProcessSettings) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, ShowFlagSettings) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, bUseRayTracingIfEnabled))
	{
		// Route through the same choke point as the runtime Tick path. In non-PIE editor no
		// captures are running, so this applies immediately. In PIE it is deferred until the
		// next safe window (TickComponent or end of SendMeasurements).
		bReconfigurePending = true;
		TryApplyPendingReconfigure();
	}
}
#endif
