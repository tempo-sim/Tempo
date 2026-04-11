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

// Root-Finding Solver (Newton-Raphson) - used for feasibility checks in BrownConrady mode
static double Solve(const TFunction<double(double)>& Objective, const TFunction<double(double)>& Derivative, const double InitialGuess, const int32 MaxIter, const double Threshold)
{
	double X = InitialGuess;
	for (int I = 0; I < MaxIter; ++I)
	{
		const double FVal = Objective(X);
		if (FMath::Abs(FVal) < Threshold)
		{
			break;
		}
		double Deriv = Derivative(X);
		if (FMath::Abs(Deriv) < 0.001)
		{
			Deriv = (Deriv < 0) ? -0.001 : 0.001;
		}
		X -= FVal / Deriv;
	}
	return X;
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
				UE_LOG(LogTemp, Warning, TEXT("No semantic ID found for instance ID %d"), InstanceId);
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
	PostProcessSettings.AutoExposureMethod = AEM_Basic;
	PostProcessSettings.AutoExposureSpeedUp = 20.0;
	PostProcessSettings.AutoExposureSpeedDown = 20.0;
	PostProcessSettings.AutoExposureLowPercent = 75.0;
	PostProcessSettings.AutoExposureHighPercent = 85.0;
	PostProcessSettings.MotionBlurAmount = 0.0;
	ShowFlags.SetAntiAliasing(true);
	ShowFlags.SetTemporalAA(true);
	ShowFlags.SetMotionBlur(false);

	// Start with no-depth settings
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	PixelFormatOverride = EPixelFormat::PF_Unknown;

	bAutoActivate = false;
}

void UTempoCameraCaptureComponent::Activate(bool bReset)
{
	// Set up depth format and materials BEFORE Super::Activate, which calls InitRenderTarget.
	// This ensures InitRenderTarget creates the render target with the correct format on the first call,
	// avoiding a double-init race condition where two render commands compete over StagingTextures.
	SetDepthEnabled(CameraOwner->bDepthEnabled);

	UE_LOG(LogTemp, Warning, TEXT("Activate"));
	
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
		PostProcessSettings.WeightedBlendables.Array.Empty();
		PostProcessSettings.WeightedBlendables.Array.Init(FWeightedBlendable(1.0, PostProcessMaterialInstance), 1);
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
	SetRelativeRotation(FRotator(PitchOffset, YawOffset, 0.0));

	if (CameraOwner->DistortionModel == ETempoDistortionModel::Equidistant)
	{
		EquidistantTileHFOVRad = FMath::DegreesToRadians(EquidistantTileFOV);
		const double TileHHalfRad = EquidistantTileHFOVRad / 2.0;
		const double TileVHalfRad = TileHHalfRad * InTileSizeXY.Y / InTileSizeXY.X;

		// Construct the distortion model with image-plane convention (negate pitch: UE pitch-up = image-Y-negative).
		const double AzRad = FMath::DegreesToRadians(YawOffset);
		const double ElRad = -FMath::DegreesToRadians(PitchOffset);
		FEquidistantTileDistortion Model(AzRad, ElRad);

		// Sample corners and edge midpoints to find the max perspective extent needed.
		double MaxTanH = 0.0;
		double MaxTanV = 0.0;
		const double SX[] = {-TileHHalfRad, TileHHalfRad, -TileHHalfRad, TileHHalfRad, 0.0, 0.0, -TileHHalfRad, TileHHalfRad};
		const double SY[] = {-TileVHalfRad, -TileVHalfRad, TileVHalfRad, TileVHalfRad, -TileVHalfRad, TileVHalfRad, 0.0, 0.0};
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector2D Source = Model.DistortedToSource(SX[I], SY[I]);
			MaxTanH = FMath::Max(MaxTanH, FMath::Abs(Source.X));
			MaxTanV = FMath::Max(MaxTanV, FMath::Abs(Source.Y));
		}

		// FOVAngle is horizontal. Vertical FOV = 2*atan(tan(hFOV/2) * H/W).
		// Need: tan(hHalf) >= MaxTanH and tan(hHalf) * tileH/tileW >= MaxTanV.
		const double AspectRatio = (InTileSizeXY.Y > 0)
			? static_cast<double>(InTileSizeXY.X) / static_cast<double>(InTileSizeXY.Y)
			: 1.0;
		const double RequiredTanHHalf = FMath::Max(MaxTanH, MaxTanV * AspectRatio);
		FOVAngle = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(RequiredTanHHalf)) * 2.0, 1.0, 170.0);
		SizeXY = InTileSizeXY;
		TileOutputOffsetY = 0;
	}
	else
	{
		SizeXY = InTileSizeXY;
		EquidistantTileHFOVRad = 0.0;
		TileOutputOffsetY = 0;
	}
}

void UTempoCameraCaptureComponent::InitDistortionMap()
{
	UE_LOG(LogTemp, Warning, TEXT("InitDistortionMap"));

	if (SizeXY.X <= 0 || SizeXY.Y <= 0)
	{
		return;
	}

	if (CameraOwner->DistortionModel == ETempoDistortionModel::BrownConrady)
	{
		// Brown-Conrady: existing distortion map logic
		const double K1 = CameraOwner->LensParameters.K1;
		const double K2 = CameraOwner->LensParameters.K2;
		const double K3 = CameraOwner->LensParameters.K3;

		// Feasibility check for barrel distortion
		double MaxDistortedRadius = -1.0;
		if (K1 < 0.0)
		{
			if (FMath::IsNearlyZero(K2) && FMath::IsNearlyZero(K3))
			{
				double RCrit = FMath::Sqrt(-1.0 / (3.0 * K1));
				double RCrit2 = RCrit * RCrit;
				MaxDistortedRadius = RCrit * (1.0 + K1 * RCrit2);
			}
			else
			{
				double RCrit = Solve(
					[K1, K2, K3](double R) {
						double R2 = R * R;
						double R4 = R2 * R2;
						double R6 = R4 * R2;
						return 1.0 + 3.0*K1*R2 + 5.0*K2*R4 + 7.0*K3*R6;
					},
					[K1, K2, K3](double R) {
						double R2 = R * R;
						double R3 = R2 * R;
						double R5 = R3 * R2;
						return 6.0*K1*R + 20.0*K2*R3 + 42.0*K3*R5;
					},
					0.5, 40, 1e-6
				);
				double R2 = RCrit * RCrit;
				double R4 = R2 * R2;
				double R6 = R4 * R2;
				double Scale = 1.0 + K1*R2 + K2*R4 + K3*R6;
				MaxDistortedRadius = RCrit * Scale;
			}
		}
		if (MaxDistortedRadius > 0.0 && CameraOwner->HorizontalFOV > 0.0f)
		{
			double MaxPossibleFOV = FMath::RadiansToDegrees(FMath::Atan(MaxDistortedRadius)) * 2.0;
			ensureMsgf(CameraOwner->HorizontalFOV <= MaxPossibleFOV, TEXT("HorizontalFOV %.2f exceeds limit %.2f for K1=%.3f. Artifacts expected."), CameraOwner->HorizontalFOV, MaxPossibleFOV, K1);
		}

		const FBrownConradyDistortion Model(K1, K2, K3);

		const double AspectRatio = static_cast<float>(CameraOwner->SizeXY.Y) / static_cast<float>(CameraOwner->SizeXY.X);
		// Compute FOVAngle (the perspective render FOV) from HorizontalFOV and lens parameters
		const float HalfFOVRad = FMath::DegreesToRadians(CameraOwner->HorizontalFOV / 2.0f);
		const double TargetRadiusHoriz = FMath::Tan(HalfFOVRad);
		const double TargetRadiusVert = AspectRatio * TargetRadiusHoriz;
		const double SourceRadiusHoriz = FBrownConradyDistortion::SolveDistortion(TargetRadiusHoriz, K1, K2, K3);
		const double SourceRadiusVert = FBrownConradyDistortion::SolveDistortion(TargetRadiusVert, K1, K2, K3);
		const double SourceAspectRatio = SourceRadiusVert / SourceRadiusHoriz;
		const float SourceHalfRadHoriz = FMath::Atan(SourceRadiusHoriz);
		const float SourceHalfRadVert = FMath::Atan(SourceRadiusVert);
		const float UndistortedFOVAngleHoriz = FMath::Clamp(FMath::RadiansToDegrees(SourceHalfRadHoriz) * 2.0f, 1.0f, 170.0f);
		const float UndistortedFOVAngleVert = FMath::Clamp(FMath::RadiansToDegrees(SourceHalfRadVert) * 2.0f, 1.0f, 170.0f);

		CreateOrResizeDistortionMapTexture();
		SizeXY.Y = SourceAspectRatio * SizeXY.X;
		const double FxDest = (SizeXY.X / 2.0) / FMath::Tan(FMath::DegreesToRadians(UndistortedFOVAngleHoriz / 2.0));
		const double FyDest = FxDest;

		const double SourceRadiusDiag = FMath::Sqrt(SourceRadiusHoriz * SourceRadiusHoriz + SourceRadiusVert * SourceRadiusVert);
		const double EndRadiusDiag = FBrownConradyDistortion::SolveInverseDistortion(SourceRadiusDiag, K1, K2, K3);
		const double EndRadiusHoriz = EndRadiusDiag / FMath::Sqrt(1.0 + SourceAspectRatio * SourceAspectRatio);
		FOVAngle = FMath::Max(FMath::RadiansToDegrees(FMath::Atan(SourceRadiusHoriz) * 2.0), FMath::RadiansToDegrees(FMath::Atan(EndRadiusHoriz) * 2.0));
		const double FxSource = (SizeXY.X / 2.0) / FMath::Tan(FMath::DegreesToRadians(FOVAngle / 2.0));
		const double FySource = FxSource;

		FillDistortionMap(Model, FxDest, FyDest, FxSource, FySource);
	}
	else // Equidistant
	{
		// Negate pitch: UE positive pitch = up, but image-plane positive Y = down.
		const double AzOffset = FMath::DegreesToRadians(GetRelativeRotation().Yaw);
		const double ElOffset = -FMath::DegreesToRadians(GetRelativeRotation().Pitch);
		const FEquidistantTileDistortion Model(AzOffset, ElOffset);

		CreateOrResizeDistortionMapTexture();

		// Equidistant focal length: pixels per radian (based on the equidistant tile's FOV).
		const double FDest = static_cast<double>(SizeXY.X) / EquidistantTileHFOVRad;

		// Perspective focal length: based on the (potentially wider) perspective render FOV.
		const double HalfPerspFOVRad = FMath::DegreesToRadians(FOVAngle / 2.0);
		const double FSource = SizeXY.X / (2.0 * FMath::Tan(HalfPerspFOVRad));

		FillDistortionMap(Model, FDest, FDest, FSource, FSource);
	}
}

// ------------------------------------------------------------------------------------
// UTempoCamera
// ------------------------------------------------------------------------------------

UTempoCamera::UTempoCamera()
{
	PrimaryComponentTick.bCanEverTick = true;
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE, EMeasurementType::BOUNDING_BOXES };
	bAutoActivate = true;
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
	if (DistortionModel == ETempoDistortionModel::BrownConrady)
	{
		ensureMsgf(HorizontalFOV <= 170.0f, TEXT("BrownConrady HorizontalFOV %.2f exceeds max 170 degrees."), HorizontalFOV);
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

	if (DistortionModel == ETempoDistortionModel::BrownConrady)
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
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, DistortionModel) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, HorizontalFOV) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, SizeXY) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, LensParameters))
	{
		SyncCaptureComponents();
	}
}
#endif
