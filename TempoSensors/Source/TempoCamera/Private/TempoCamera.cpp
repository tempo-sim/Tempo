// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"

#include "TempoActorLabeler.h"
#include "TempoCameraModule.h"
#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"
#include "TempoLabelTypes.h"
#include "TempoSensorsConstants.h"
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
// UTempoCameraCaptureComponent
// ------------------------------------------------------------------------------------

UTempoCameraCaptureComponent::UTempoCameraCaptureComponent()
{
	// Tiles output HDR linear color so tonemapping runs once on the merged output rather than
	// independently per tile (which would mismatch exposure across the stitched image).
	CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;

	// fp32 RGBA tile RT: RGB carries HDR linear color, A carries asfloat((label<<24) | depth24).
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA32f;
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

	// Tiles must not do any per-tile exposure adaptation — divergent per-tile ViewStates would
	// produce mismatched brightness across the stitched image. Tonemap runs once on the proxy.
	PostProcessSettings.bOverride_AutoExposureMethod = true;
	PostProcessSettings.AutoExposureMethod = AEM_Manual;

	// Drop the proxy tonemap PPM — it reads SharedTextureTarget, which tiles do not populate —
	// but preserve any user-authored blendables.
	PostProcessSettings.WeightedBlendables.Array.RemoveAll([CameraOwner = this->CameraOwner](const FWeightedBlendable& WB)
	{
		return WB.Object != nullptr && WB.Object == CameraOwner->ProxyTonemapMID;
	});

	// Re-append the distortion/label post-process material if it was already created
	// (on first call from Configure it is still null and will be added by SetDepthEnabled).
	if (PostProcessMaterialInstance)
	{
		PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0, PostProcessMaterialInstance));
	}

	SetShowFlagSettings(CameraOwner->GetShowFlagSettings());

	bUseRayTracingIfEnabled = CameraOwner->bUseRayTracingIfEnabled;
}

void UTempoCameraCaptureComponent::Activate(bool bReset)
{
	// Must set depth format and materials before Super::Activate calls InitRenderTarget, so the
	// render target is created with the correct format on the first call.
	SetDepthEnabled(CameraOwner->bDepthEnabled);

	Super::Activate(bReset);

	if (TextureTarget)
	{
		// The tile RT is fp32 HDR linear; override the base SceneCaptureGamma so values are
		// stored untouched and the proxy tonemap can apply its own gamma.
		TextureTarget->TargetGamma = 1.0f;
		// Point sampling: the A channel carries bit-packed (label, depth) via asfloat. Bilinear
		// filtering would smear adjacent pixels' bit patterns and break asuint() decoding on the
		// aux side. Stitch passes also draw at 1:1 pixel ratios, so point is correct for RGB too.
		TextureTarget->Filter = TextureFilter::TF_Nearest;
	}
}

void UTempoCameraCaptureComponent::SetDepthEnabled(bool bDepthEnabled)
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	// Tile RT format (fp32 RGBA) is set in the constructor and no longer depends on bDepthEnabled.
	// Only the distortion/label PPM choice varies with depth.
	if (bDepthEnabled)
	{
		if (const TObjectPtr<UMaterialInterface> PostProcessMaterialWithDepth = TempoSensorsSettings->GetCameraPostProcessMaterialWithDepth())
		{
			PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterialWithDepth.Get(), this);
			ApplyDistortionMapToMaterial(PostProcessMaterialInstance);
			MinDepth = GEngine->NearClipPlane;
			MaxDepth = TempoSensorsSettings->GetMaxCameraDepth();
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), MinDepth);
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), MaxDepth);
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempoCamera_Max_Discrete_Depth);
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
		// Rebuild blendables from the owner's user-authored list, then append the distortion/label
		// material. Filter out the owner's ProxyTonemapMID — that PPM reads SharedTextureTarget,
		// which tiles populate, and applying it inside a tile would feed stale stitched content
		// back into the tile's scene color.
		PostProcessSettings.WeightedBlendables.Array.Empty();
		if (CameraOwner)
		{
			PostProcessSettings.WeightedBlendables.Array.Append(CameraOwner->PostProcessSettings.WeightedBlendables.Array);
			PostProcessSettings.WeightedBlendables.Array.RemoveAll([CameraOwner = this->CameraOwner](const FWeightedBlendable& WB)
			{
				return WB.Object != nullptr && WB.Object == CameraOwner->ProxyTonemapMID;
			});
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

UMaterialInstanceDynamic* UTempoCameraCaptureComponent::GetOrCreateStitchMID()
{
	if (!TextureTarget)
	{
		return nullptr;
	}

	if (!StitchMID)
	{
		const UTempoSensorsSettings* Settings = GetDefault<UTempoSensorsSettings>();
		UMaterialInterface* StitchMat = Settings ? Settings->GetCameraStitchPassthroughMaterial().Get() : nullptr;
		if (!StitchMat)
		{
			UE_LOG(LogTempoCamera, Error, TEXT("CameraStitchPassthroughMaterial is not set in TempoSensors settings."));
			return nullptr;
		}
		StitchMID = UMaterialInstanceDynamic::Create(StitchMat, this);
	}

	// Rebind every call — cheap, and correctly tracks TextureTarget swaps on reactivate.
	StitchMID->SetTextureParameterValue(TEXT("TileRT"), TextureTarget);
	return StitchMID;
}

UMaterialInstanceDynamic* UTempoCameraCaptureComponent::GetOrCreateStitchAuxMID()
{
	if (!TextureTarget)
	{
		return nullptr;
	}

	if (!StitchAuxMID)
	{
		const UTempoSensorsSettings* Settings = GetDefault<UTempoSensorsSettings>();
		UMaterialInterface* StitchMat = Settings ? Settings->GetCameraStitchAuxMaterial().Get() : nullptr;
		if (!StitchMat)
		{
			UE_LOG(LogTempoCamera, Error, TEXT("CameraStitchAuxMaterial is not set in TempoSensors settings."));
			return nullptr;
		}
		StitchAuxMID = UMaterialInstanceDynamic::Create(StitchMat, this);
	}

	StitchAuxMID->SetTextureParameterValue(TEXT("TileRT"), TextureTarget);
	return StitchAuxMID;
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

	// UTempoCamera itself doubles as the proxy scene capture: its PPM replaces scene color with
	// the stitched HDR texture, then bloom/AE/tonemapper produce LDR for the merge pass to pack
	// together with label+depth bytes.
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	PixelFormatOverride = EPixelFormat::PF_Unknown;

	// The proxy's scene render is useless — its PPM overwrites scene color before bloom/AE. Hide
	// world geometry and lighting so nothing gets rasterized. AntiAliasing / TemporalAA /
	// MotionBlur show flags are applied below so the tonemap pass still anti-aliases cleanly.
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

	TArray<FEngineShowFlagsSetting> NewShowFlagSettings = GetShowFlagSettings();
	NewShowFlagSettings.Add({ TEXT("AntiAliasing"), true });
	NewShowFlagSettings.Add({ TEXT("TemporalAA"), true });
	NewShowFlagSettings.Add({ TEXT("MotionBlur"), false });
	SetShowFlagSettings(NewShowFlagSettings);
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

	// SharedTextureTarget carries HDR linear color produced by the HDR-capturing tiles (fp32)
	// through the passthrough stitch. fp16 is plenty of precision for linear color and halves
	// the bandwidth vs fp32. The proxy scene capture's PPM samples this as HDRColorRT, and
	// Bloom/AE/Tonemapper produce the LDR result in the inherited TextureTarget.
	SharedTextureTarget = NewObject<UTextureRenderTarget2D>(this);
	SharedTextureTarget->TargetGamma = 1.0f;
	SharedTextureTarget->bGPUSharedFlag = true;
	SharedTextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
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

	// Require that each active tile has a valid render target. Tile format (fp32) intentionally
	// differs from SharedTextureTarget (fp16) — the Canvas stitch samples fp32 and writes fp16.
	for (const UTempoCameraCaptureComponent* Tile : ActiveCaptureComponents)
	{
		if (!Tile->TextureTarget || !Tile->TextureTarget->GameThread_GetRenderTargetResource())
		{
			return;
		}
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

	// Capture each tile. With CaptureScene() (not CaptureSceneDeferred) the tile's render commands
	// enqueue immediately in FIFO order, so the Canvas stitch we issue right after will run
	// after all tile renders on the render thread.
	for (UTempoCameraCaptureComponent* Tile : ActiveCaptureComponents)
	{
		Tile->CaptureScene();
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

	// Canvas-based stitch: for each tile, draw its TextureTarget sub-rect into SharedTextureTarget
	// at TileDestOffset using the passthrough material. Enqueues render commands in FIFO order after
	// the tile CaptureScene calls above.
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize(0.0, 0.0);
		FDrawToRenderTargetContext Ctx;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, SharedTextureTarget, Canvas, CanvasSize, Ctx);

		for (UTempoCameraCaptureComponent* Tile : ActiveCaptureComponents)
		{
			UMaterialInstanceDynamic* MID = Tile->GetOrCreateStitchMID();
			if (!MID)
			{
				continue;
			}

			const int32 TileRTSizeX = Tile->TextureTarget->SizeX;
			const int32 TileRTSizeY = Tile->TextureTarget->SizeY;
			if (TileRTSizeX <= 0 || TileRTSizeY <= 0)
			{
				continue;
			}

			const FVector2D UV0(0.0, static_cast<double>(Tile->TileOutputOffsetY) / TileRTSizeY);
			const FVector2D UV1(static_cast<double>(Tile->TileOutputSizeXY.X) / TileRTSizeX,
				static_cast<double>(Tile->TileOutputOffsetY + Tile->TileOutputSizeXY.Y) / TileRTSizeY);

			FCanvasTileItem Item(
				FVector2D(Tile->TileDestOffset),
				MID->GetRenderProxy(),
				FVector2D(Tile->TileOutputSizeXY),
				UV0,
				UV1);
			Item.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(Item);
		}

		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
	}

	// Aux stitch pass: samples each tile's fp32 A channel and unpacks (label, depth) bits into
	// RGBA8 bytes in SharedAuxTextureTarget for the merge pass to combine with tonemapped color.
	if (SharedAuxTextureTarget)
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize(0.0, 0.0);
		FDrawToRenderTargetContext Ctx;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, SharedAuxTextureTarget, Canvas, CanvasSize, Ctx);

		for (UTempoCameraCaptureComponent* Tile : ActiveCaptureComponents)
		{
			UMaterialInstanceDynamic* MID = Tile->GetOrCreateStitchAuxMID();
			if (!MID)
			{
				continue;
			}

			const int32 TileRTSizeX = Tile->TextureTarget->SizeX;
			const int32 TileRTSizeY = Tile->TextureTarget->SizeY;
			if (TileRTSizeX <= 0 || TileRTSizeY <= 0)
			{
				continue;
			}

			const FVector2D UV0(0.0, static_cast<double>(Tile->TileOutputOffsetY) / TileRTSizeY);
			const FVector2D UV1(static_cast<double>(Tile->TileOutputSizeXY.X) / TileRTSizeX,
				static_cast<double>(Tile->TileOutputOffsetY + Tile->TileOutputSizeXY.Y) / TileRTSizeY);

			FCanvasTileItem Item(
				FVector2D(Tile->TileDestOffset),
				MID->GetRenderProxy(),
				FVector2D(Tile->TileOutputSizeXY),
				UV0,
				UV1);
			Item.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(Item);
		}

		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
	}

	// Proxy scene capture: the PPM (appended to PostProcessSettings.WeightedBlendables by
	// GetOrCreateProxyTonemapMID) replaces scene color with SharedTextureTarget's HDR linear
	// color before Bloom/AE/Tonemapper, so the tonemapped LDR output landing in the inherited
	// TextureTarget is effectively tonemap(stitched HDR).
	if (GetOrCreateProxyTonemapMID())
	{
		CaptureScene();
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
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, HorizontalFOV) ||
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
