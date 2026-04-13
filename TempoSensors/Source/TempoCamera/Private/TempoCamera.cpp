// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"

#include "TempoActorLabeler.h"
#include "TempoCameraModule.h"
#include "TempoLabelTypes.h"
#include "TempoSensorsConstants.h"
#include "TempoSensorsSettings.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Box2D.h"
#include "TextureResource.h"

namespace
{
	const FName TLCaptureComponentName(TEXT("TopLeftTempoCameraCaptureComponent"));
	const FName TRCaptureComponentName(TEXT("TopRightTempoCameraCaptureComponent"));
	const FName BLCaptureComponentName(TEXT("BottomLeftTempoCameraCaptureComponent"));
	const FName BRCaptureComponentName(TEXT("BottomRightTempoCameraCaptureComponent"));

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

	ShowFlagSettings = CameraOwner->ShowFlagSettings;
	for (const FEngineShowFlagsSetting& Setting : ShowFlagSettings)
	{
		const int32 SettingIndex = FEngineShowFlags::FindIndexByName(*Setting.ShowFlagName);
		if (SettingIndex != INDEX_NONE)
		{
			ShowFlags.SetSingleFlag(SettingIndex, Setting.Enabled);
		}
	}
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

TUniquePtr<FDistortionModel> UTempoCameraCaptureComponent::CreateDistortionModel() const
{
	if (CameraOwner->LensParameters.DistortionModel == ETempoDistortionModel::BrownConrady)
	{
		return MakeUnique<FBrownConradyDistortion>(
			CameraOwner->LensParameters.K1,
			CameraOwner->LensParameters.K2,
			CameraOwner->LensParameters.K3);
	}
	else if (CameraOwner->LensParameters.DistortionModel == ETempoDistortionModel::Rational)
	{
		return MakeUnique<FRationalDistortion>(
			CameraOwner->LensParameters.K1,
			CameraOwner->LensParameters.K2,
			CameraOwner->LensParameters.K3,
			CameraOwner->LensParameters.K4,
			CameraOwner->LensParameters.K5,
			CameraOwner->LensParameters.K6);
	}
	else // Equidistant (Kannala-Brandt fisheye; K=0 = pure equidistant)
	{
		// Negate pitch: UE positive pitch = up, but image-plane positive Y = down.
		const double AzOffset = FMath::DegreesToRadians(GetRelativeRotation().Yaw);
		const double ElOffset = -FMath::DegreesToRadians(GetRelativeRotation().Pitch);
		return MakeUnique<FKannalaBrandtDistortion>(
			CameraOwner->LensParameters.K1,
			CameraOwner->LensParameters.K2,
			CameraOwner->LensParameters.K3,
			CameraOwner->LensParameters.K4,
			AzOffset, ElOffset);
	}
}

void UTempoCameraCaptureComponent::InitDistortionMap()
{
	if (TileOutputSizeXY.X <= 0 || TileOutputSizeXY.Y <= 0)
	{
		return;
	}

	TUniquePtr<FDistortionModel> Model = CreateDistortionModel();
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
	PostProcessSettings.AutoExposureMethod = AEM_Basic;
	PostProcessSettings.AutoExposureSpeedUp = 20.0;
	PostProcessSettings.AutoExposureSpeedDown = 20.0;
	PostProcessSettings.AutoExposureLowPercent = 75.0;
	PostProcessSettings.AutoExposureHighPercent = 85.0;
	PostProcessSettings.MotionBlurAmount = 0.0;

	ShowFlagSettings.Add({ TEXT("AntiAliasing"), true });
	ShowFlagSettings.Add({ TEXT("TemporalAA"), true });
	ShowFlagSettings.Add({ TEXT("MotionBlur"), false });
}

void UTempoCamera::OnRegister()
{
	Super::OnRegister();

	// Don't activate capture components during cooking
	if (IsRunningCommandlet())
	{
		return;
	}

	SyncCaptureComponents();
}

void UTempoCamera::BeginPlay()
{
	Super::BeginPlay();
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
	for (const UTempoCameraCaptureComponent* CaptureComponent : GetActiveCaptureComponents())
	{
		if (CaptureComponent->IsNextReadAwaitingRender())
		{
			return true;
		}
	}
	return false;
}

void UTempoCamera::OnRenderCompleted()
{
	for (UTempoCameraCaptureComponent* CaptureComponent : GetActiveCaptureComponents())
	{
		CaptureComponent->ReadNextIfAvailable();
	}
}

void UTempoCamera::BlockUntilMeasurementsReady() const
{
	for (const UTempoCameraCaptureComponent* CaptureComponent : GetActiveCaptureComponents())
	{
		CaptureComponent->BlockUntilNextReadComplete();
	}
}

TOptional<TFuture<void>> UTempoCamera::SendMeasurements()
{
	const TArray<UTempoCameraCaptureComponent*> ActiveCaptureComponents = GetActiveCaptureComponents();
	const int32 NumCompletedReads = ActiveCaptureComponents.FilterByPredicate(
		[](const UTempoCameraCaptureComponent* CaptureComponent) { return CaptureComponent->NextReadComplete(); }
	).Num();

	TOptional<TFuture<void>> Future;

	if (NumCompletedReads == ActiveCaptureComponents.Num())
	{
		TArray<TUniquePtr<FTextureRead>> TextureReads;

		for (UTempoCameraCaptureComponent* CaptureComponent : ActiveCaptureComponents)
		{
			TextureReads.Add(CaptureComponent->DequeueIfReadComplete());
		}

		if (!bDepthEnabled && !PendingDepthImageRequests.IsEmpty())
		{
			SetDepthEnabled(true);
		}
		if (bDepthEnabled && PendingDepthImageRequests.IsEmpty())
		{
			SetDepthEnabled(false);
		}

		if (TextureReads.Num() == 1)
		{
			Future = DecodeAndRespond(MoveTemp(TextureReads[0]));
		}
		else if (TextureReads.Num() > 1)
		{
			TArray<FCameraTileTextureRead> TileReads;
			for (int32 i = 0; i < TextureReads.Num(); ++i)
			{
				FCameraTileTextureRead TileRead;
				TileRead.TextureRead = MoveTemp(TextureReads[i]);
				TileRead.TileDestOffset = ActiveCaptureComponents[i]->TileDestOffset;
				TileRead.TileOutputSizeXY = ActiveCaptureComponents[i]->TileOutputSizeXY;
				TileRead.TileOutputOffsetY = ActiveCaptureComponents[i]->TileOutputOffsetY;
				TileReads.Add(MoveTemp(TileRead));
			}
			Future = StitchAndRespond(MoveTemp(TileReads));
		}

		PendingColorImageRequests.Empty();
		PendingLabelImageRequests.Empty();
		PendingBoundingBoxesRequests.Empty();
		if (bDepthEnabled)
		{
			PendingDepthImageRequests.Empty();
		}
		SequenceId++;
	}

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

template <typename PixelType>
TUniquePtr<TTextureRead<PixelType>> StitchTiles(
	const FIntPoint& OutputSize,
	TArray<FCameraTileTextureRead>& TileReads,
	int32 SequenceId, double CaptureTime, const FString& OwnerName, const FString& SensorName,
	const FTransform& SensorTransform, float MinDepth, float MaxDepth, TMap<uint8, uint8>&& InstanceToSemanticMap)
{
	TUniquePtr<TTextureRead<PixelType>> Stitched;
	if constexpr (PixelType::bSupportsDepth)
	{
		Stitched = MakeUnique<TTextureRead<PixelType>>(OutputSize, SequenceId, CaptureTime, OwnerName, SensorName, SensorTransform, MinDepth, MaxDepth, MoveTemp(InstanceToSemanticMap));
	}
	else
	{
		Stitched = MakeUnique<TTextureRead<PixelType>>(OutputSize, SequenceId, CaptureTime, OwnerName, SensorName, SensorTransform, MoveTemp(InstanceToSemanticMap));
	}

	// Copy tile pixels into the stitched output
	for (const auto& Tile : TileReads)
	{
		const auto* TileRead = static_cast<const TTextureRead<PixelType>*>(Tile.TextureRead.Get());
		const int32 TileWidth = Tile.TileOutputSizeXY.X;
		const int32 TileHeight = Tile.TileOutputSizeXY.Y;
		const int32 SrcStride = TileRead->ImageSize.X; // render target width (== TileWidth for camera tiles)
		const int32 SrcOffsetY = Tile.TileOutputOffsetY;
		const int32 DstOffsetX = Tile.TileDestOffset.X;
		const int32 DstOffsetY = Tile.TileDestOffset.Y;

		ParallelFor(TileHeight, [&](int32 Row)
		{
			const int32 SrcIdx = (SrcOffsetY + Row) * SrcStride;
			const int32 DstIdx = (DstOffsetY + Row) * OutputSize.X + DstOffsetX;
			FMemory::Memcpy(&Stitched->Image[DstIdx], &TileRead->Image[SrcIdx], TileWidth * sizeof(PixelType));
		});
	}

	return Stitched;
}

TFuture<void> UTempoCamera::StitchAndRespond(TArray<FCameraTileTextureRead> TileReads)
{
	if (TileReads.IsEmpty())
	{
		return Async(EAsyncExecution::TaskGraph, [](){});
	}

	const double TransmissionTime = GetWorld()->GetTimeSeconds();
	const bool bWithDepth = TileReads[0].TextureRead->GetType() == TEXT("WithDepth");

	// Get the instance-to-semantic map from the first tile (all tiles share the same map)
	TMap<uint8, uint8> InstanceToSemanticMap;
	if (bWithDepth)
	{
		InstanceToSemanticMap = static_cast<TTextureRead<FCameraPixelWithDepth>*>(TileReads[0].TextureRead.Get())->InstanceToSemanticMap;
	}
	else
	{
		InstanceToSemanticMap = static_cast<TTextureRead<FCameraPixelNoDepth>*>(TileReads[0].TextureRead.Get())->InstanceToSemanticMap;
	}

	return Async(EAsyncExecution::TaskGraph, [
		OutputSize = SizeXY,
		TileReadsMoved = MoveTemp(TileReads),
		SequenceIdCpy = SequenceId,
		CaptureTime = GetWorld()->GetTimeSeconds(),
		OwnerNameCpy = GetOwnerName(),
		SensorNameCpy = GetSensorName(),
		SensorTransformCpy = GetComponentTransform(),
		MinDepthCpy = MinDepth,
		MaxDepthCpy = MaxDepth,
		InstanceToSemanticMapMoved = MoveTemp(InstanceToSemanticMap),
		ColorImageRequests = PendingColorImageRequests,
		LabelImageRequests = PendingLabelImageRequests,
		DepthImageRequests = PendingDepthImageRequests,
		BoundingBoxRequests = PendingBoundingBoxesRequests,
		TransmissionTimeCpy = TransmissionTime,
		bWithDepthCpy = bWithDepth
	]() mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraStitchAndRespond);

		if (bWithDepthCpy)
		{
			auto Stitched = StitchTiles<FCameraPixelWithDepth>(
				OutputSize, TileReadsMoved, SequenceIdCpy, CaptureTime,
				OwnerNameCpy, SensorNameCpy, SensorTransformCpy,
				MinDepthCpy, MaxDepthCpy, MoveTemp(InstanceToSemanticMapMoved));

			Stitched->RespondToRequests(ColorImageRequests, TransmissionTimeCpy);
			Stitched->RespondToRequests(LabelImageRequests, TransmissionTimeCpy);
			Stitched->RespondToRequests(DepthImageRequests, TransmissionTimeCpy);
			Stitched->RespondToRequests(BoundingBoxRequests, TransmissionTimeCpy);
		}
		else
		{
			auto Stitched = StitchTiles<FCameraPixelNoDepth>(
				OutputSize, TileReadsMoved, SequenceIdCpy, CaptureTime,
				OwnerNameCpy, SensorNameCpy, SensorTransformCpy,
				0.0f, 0.0f, MoveTemp(InstanceToSemanticMapMoved));

			Stitched->RespondToRequests(ColorImageRequests, TransmissionTimeCpy);
			Stitched->RespondToRequests(LabelImageRequests, TransmissionTimeCpy);
			Stitched->RespondToRequests(BoundingBoxRequests, TransmissionTimeCpy);
		}
	});
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
		for (const FName& Tag : { TLCaptureComponentName, TRCaptureComponentName, BLCaptureComponentName, BRCaptureComponentName })
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
	for (const FName& Tag : { TLCaptureComponentName, TRCaptureComponentName, BLCaptureComponentName, BRCaptureComponentName })
	{
		if (!CaptureComponents.Contains(Tag))
		{
			UTempoCameraCaptureComponent* CaptureComponent = NewObject<UTempoCameraCaptureComponent>(GetOwner(), UTempoCameraCaptureComponent::StaticClass(), Tag);
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
			DistortionModel == ETempoDistortionModel::Rational ? TEXT("Rational") : TEXT("BrownConrady"), HorizontalFOV);
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

	if (LensParameters.DistortionModel == ETempoDistortionModel::BrownConrady || LensParameters.DistortionModel == ETempoDistortionModel::Rational)
	{
		// Single capture: use TL, deactivate others
		SyncCaptureComponent(CaptureComponents[TLCaptureComponentName], true, 0.0, 0.0, HorizontalFOV, SizeXY);
		SyncCaptureComponent(CaptureComponents[TRCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		SyncCaptureComponent(CaptureComponents[BLCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		SyncCaptureComponent(CaptureComponents[BRCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
	}
	else // Equidistant
	{
		const double VerticalFOV = HorizontalFOV * static_cast<double>(SizeXY.Y) / static_cast<double>(SizeXY.X);
		const bool bSplitHorizontal = HorizontalFOV > MaxPerspectiveFOVPerCapture;
		const bool bSplitVertical = VerticalFOV > MaxPerspectiveFOVPerCapture;

		if (!bSplitHorizontal && !bSplitVertical)
		{
			// 1 capture
			SyncCaptureComponent(CaptureComponents[TLCaptureComponentName], true, 0.0, 0.0, HorizontalFOV, SizeXY);
			SyncCaptureComponent(CaptureComponents[TRCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
			SyncCaptureComponent(CaptureComponents[BLCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
			SyncCaptureComponent(CaptureComponents[BRCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		}
		else if (bSplitHorizontal && !bSplitVertical)
		{
			// 2 captures: left + right
			const double SubFOV = HorizontalFOV / 2.0;
			const double YawOffset = SubFOV / 2.0;
			const int32 LeftWidth = FMath::CeilToInt32(SizeXY.X / 2.0);
			const int32 RightWidth = SizeXY.X - LeftWidth;

			SyncCaptureComponent(CaptureComponents[TLCaptureComponentName], true, -YawOffset, 0.0, SubFOV, FIntPoint(LeftWidth, SizeXY.Y), FIntPoint(0, 0));
			SyncCaptureComponent(CaptureComponents[TRCaptureComponentName], true, YawOffset, 0.0, SubFOV, FIntPoint(RightWidth, SizeXY.Y), FIntPoint(LeftWidth, 0));
			SyncCaptureComponent(CaptureComponents[BLCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
			SyncCaptureComponent(CaptureComponents[BRCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
		}
		else if (!bSplitHorizontal && bSplitVertical)
		{
			// 2 captures: top + bottom
			const double SubVertFOV = VerticalFOV / 2.0;
			const double PitchOffset = SubVertFOV / 2.0;
			const int32 TopHeight = FMath::CeilToInt32(SizeXY.Y / 2.0);
			const int32 BottomHeight = SizeXY.Y - TopHeight;

			SyncCaptureComponent(CaptureComponents[TLCaptureComponentName], true, 0.0, PitchOffset, HorizontalFOV, FIntPoint(SizeXY.X, TopHeight), FIntPoint(0, 0));
			SyncCaptureComponent(CaptureComponents[BLCaptureComponentName], true, 0.0, -PitchOffset, HorizontalFOV, FIntPoint(SizeXY.X, BottomHeight), FIntPoint(0, TopHeight));
			SyncCaptureComponent(CaptureComponents[TRCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
			SyncCaptureComponent(CaptureComponents[BRCaptureComponentName], false, 0.0, 0.0, 0.0, FIntPoint::ZeroValue);
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

			SyncCaptureComponent(CaptureComponents[TLCaptureComponentName], true, -YawOffset, PitchOffset, SubHFOV, FIntPoint(LeftWidth, TopHeight), FIntPoint(0, 0));
			SyncCaptureComponent(CaptureComponents[TRCaptureComponentName], true, YawOffset, PitchOffset, SubHFOV, FIntPoint(RightWidth, TopHeight), FIntPoint(LeftWidth, 0));
			SyncCaptureComponent(CaptureComponents[BLCaptureComponentName], true, -YawOffset, -PitchOffset, SubHFOV, FIntPoint(LeftWidth, BottomHeight), FIntPoint(0, TopHeight));
			SyncCaptureComponent(CaptureComponents[BRCaptureComponentName], true, YawOffset, -PitchOffset, SubHFOV, FIntPoint(RightWidth, BottomHeight), FIntPoint(LeftWidth, TopHeight));
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
}

#if WITH_EDITOR
void UTempoCamera::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, LensParameters.DistortionModel) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, HorizontalFOV) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, SizeXY) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, LensParameters) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, PostProcessSettings) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, ShowFlagSettings))
	{
		SyncCaptureComponents();
	}
}
#endif
