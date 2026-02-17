// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorLabeler.h"
#include "TempoCamera.h"
#include "TempoCameraModule.h"
#include "TempoLabelTypes.h"
#include "TempoSensorsConstants.h"
#include "TempoSensorsSettings.h"

#include "Async/Async.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Box2D.h"
#include "TextureResource.h"

// Root-Finding Solver (Newton-Raphson)
static double Solve(TFunction<double(double)> Objective, TFunction<double(double)> Derivative, double InitialGuess, int32 MaxIter, double Threshold)
{
	double X = InitialGuess;

	for (int i = 0; i < MaxIter; ++i)
	{
	   double FVal = Objective(X);
	   
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

static double SolveInverseDistortion(double TargetRadius, double K1, double K2, double K3)
{
	if (TargetRadius < 1e-6)
	{
	   return TargetRadius;
	}

	return Solve(
	   [K1, K2, K3, TargetRadius](double R) {
		 double R2 = R * R;
		 double R4 = R2 * R2;
		 double R6 = R4 * R2;
		 return (R * (1.0 + K1*R2 + K2*R4 + K3*R6)) - TargetRadius; 
	   },
	   [K1, K2, K3](double R) {
		 double R2 = R * R;
		 double R4 = R2 * R2;
		 double R6 = R4 * R2;
		 return 1.0 + 3.0*K1*R2 + 5.0*K2*R4 + 7.0*K3*R6;
	   },
	   TargetRadius,
	   40,
	   1e-6
	);
}

/**
 * Compute 2D bounding boxes from label data.
 * Single-pass algorithm: scan all pixels, maintain min/max coords per instance ID.
 * @param LabelData Array of label values (one per pixel)
 * @param Width Image width in pixels
 * @param Height Image height in pixels
 * @return Map of instance ID to bounding box
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
	   if (InstanceId > 0)  // 0 = unlabeled
	   {
	   Boxes.FindOrAdd(InstanceId) += FUintPoint(X, Y);
	   }
	   }
	}
	return Boxes;
}

FTempoCameraIntrinsics::FTempoCameraIntrinsics(const FIntPoint& SizeXY, float HorizontalFOV)
	: Fx(SizeXY.X / 2.0 / FMath::Tan(FMath::DegreesToRadians(HorizontalFOV) / 2.0)),
	  Fy(Fx), // Fx == Fy means the sensor's pixels are square, not that the FOV is symmetric.
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
	   ParallelFor(TextureRead->Image.Num(), [&ImageData, &TextureRead, Encoding](int32 Idx){
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

	   // Add bounding boxes to proto message using the map captured at render time
	   for (const auto& [InstanceId, Box] : BoundingBoxes)
	   {
	   TempoCamera::BoundingBox2D* BBoxProto = Response.add_bounding_boxes();
	   BBoxProto->set_min_x(FMath::RoundToInt32(Box.Min.X));
	   BBoxProto->set_min_y(FMath::RoundToInt32(Box.Min.Y));
	   BBoxProto->set_max_x(FMath::RoundToInt32(Box.Max.X));
	   BBoxProto->set_max_y(FMath::RoundToInt32(Box.Max.Y));
	   BBoxProto->set_instance_id(InstanceId);

	   // Find semantic ID from mapping captured at render time
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

UTempoCamera::UTempoCamera()
{
	DistortionMapTexture = nullptr;
	LensParameters.K1 = 0.0f;
	LensParameters.K2 = 0.0f;
	LensParameters.K3 = 0.0f;
	LensParameters.P1 = 0.0f;
	LensParameters.P2 = 0.0f;
	
	// Default F/C (Will be overwritten by InitDistortionMap)
	CroppingFactor = 0.0f;
	
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE, EMeasurementType::BOUNDING_BOXES};
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	PostProcessSettings.AutoExposureMethod = AEM_Basic;
	PostProcessSettings.AutoExposureSpeedUp = 20.0;
	PostProcessSettings.AutoExposureSpeedDown = 20.0;
	// Auto exposure percentages chosen to better match their own recommended settings (see Scene.h).
	PostProcessSettings.AutoExposureLowPercent = 75.0;
	PostProcessSettings.AutoExposureHighPercent = 85.0;
	PostProcessSettings.MotionBlurAmount = 0.0;
	ShowFlags.SetAntiAliasing(true);
	ShowFlags.SetTemporalAA(true);
	ShowFlags.SetMotionBlur(false);

	// Start with no-depth settings
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8; // Corresponds to PF_B8G8R8A8
	PixelFormatOverride = EPixelFormat::PF_Unknown;
}

void UTempoCamera::BeginPlay()
{
	Super::BeginPlay();
	ApplyDepthEnabled();
}

void UTempoCamera::InitRenderTarget()
{
	InitDistortionMap();
	Super::InitRenderTarget();
}

void UTempoCamera::UpdateLensParameters()
{
	if (LensParameters != LastLensParameters || !FMath::IsNearlyEqual(DistortedFOV, LastDistortedFOV) || SizeXY != LastSizeXY)
	{
	   LastLensParameters = LensParameters;
	   LastDistortedFOV = DistortedFOV;
	   LastSizeXY = SizeXY;
	   InitDistortionMap();
	}
}

void UTempoCamera::InitDistortionMap()
{
	const double K1 = LensParameters.K1;
	const double K2 = LensParameters.K2;
	const double K3 = LensParameters.K3;

	//Feasibility check
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
		   0.5,
		   40,
		   1e-6
		 );
		 
		 double R2 = RCrit * RCrit;
		 double R4 = R2 * R2;
		 double R6 = R4 * R2;
		 double Scale = 1.0 + K1*R2 + K2*R4 + K3*R6;
		 MaxDistortedRadius = RCrit * Scale;
	   }
	}
	
	if (MaxDistortedRadius > 0.0 && DistortedFOV > 0.0f)
	{
	   double MaxPossibleFOV = FMath::RadiansToDegrees(FMath::Atan(MaxDistortedRadius)) * 2.0;
	   ensureMsgf(DistortedFOV <= MaxPossibleFOV, TEXT("DistortedFOV %.2f exceeds limit %.2f for K1=%.3f. Artifacts expected."), DistortedFOV, MaxPossibleFOV, K1);
	}

	if (DistortedFOV <= 0.0f)
	{
	   FOVAngle = 90.0f;
	}
	else
	{
	   const float HalfFOVRad = FMath::DegreesToRadians(DistortedFOV / 2.0f);
	   const double TargetRadiusHoriz = FMath::Tan(HalfFOVRad);
	   
	   const double AspectRatio = (SizeXY.Y > 0) ? (double)SizeXY.X / (double)SizeXY.Y : 1.0;
	   
	   const double TargetRadiusVert = TargetRadiusHoriz / AspectRatio;
	   const double TargetRadiusDiag = FMath::Sqrt(TargetRadiusHoriz * TargetRadiusHoriz + TargetRadiusVert * TargetRadiusVert);
	   
	   const double SourceRadiusDiag = SolveInverseDistortion(TargetRadiusDiag, K1, K2, K3);
	   
	   const double GeometricRatio = TargetRadiusHoriz / TargetRadiusDiag; 
	   const double SourceRadiusHoriz = SourceRadiusDiag * GeometricRatio;
	   
	   const float SourceHalfRad = FMath::Atan(SourceRadiusHoriz);
	   FOVAngle = FMath::RadiansToDegrees(SourceHalfRad) * 2.0f;
	   
	   // Clamp to prevent rendering errors at extreme FOVs
	   FOVAngle = FMath::Clamp(FOVAngle, 1.0f, 170.0f);
	}

	if (SizeXY.X <= 0 || SizeXY.Y <= 0)
	{
	   return;
	}
	
	if (!DistortionMapTexture || DistortionMapTexture->GetSizeX() != SizeXY.X || DistortionMapTexture->GetSizeY() != SizeXY.Y)
	{
	   DistortionMapTexture = UTexture2D::CreateTransient(SizeXY.X, SizeXY.Y, PF_G16R16F);
	   DistortionMapTexture->CompressionSettings = TC_HDR;
	   DistortionMapTexture->Filter = TF_Bilinear;
	   DistortionMapTexture->AddressX = TA_Clamp;
	   DistortionMapTexture->AddressY = TA_Clamp;
	   DistortionMapTexture->SRGB = 0;
#ifdef UpdateResource
#undef UpdateResource
#endif
	   DistortionMapTexture->UpdateResource();
	}
	const float HalfRadDest = FMath::DegreesToRadians(DistortedFOV / 2.0f);
	const float NormalizedFDest = 0.5f / FMath::Tan(HalfRadDest);
	
	const double FxDest = NormalizedFDest * SizeXY.X;
	const double FyDest = FxDest;
	
	float NormalizedFSource = 0.5f;
	if (ProjectionType == ECameraProjectionMode::Perspective)
	{
	   const float ClampedFOV = FMath::Clamp(FOVAngle, 1.0f, 179.0f);
	   const float HalfRadSource = FMath::DegreesToRadians(ClampedFOV / 2.0f);
	   NormalizedFSource = 0.5f / FMath::Tan(HalfRadSource);
	}
	const double FxSource = NormalizedFSource * SizeXY.X;
	const double FySource = FxSource; 
	
	FTexture2DMipMap& Mip = DistortionMapTexture->GetPlatformData()->Mips[0];
	uint16* MipData = reinterpret_cast<uint16*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

	if (!MipData)
	{
	   Mip.BulkData.Unlock();
	   return;
	}
	
	const double Cx = SizeXY.X * 0.5;
	const double Cy = SizeXY.Y * 0.5;
	
	const double InvFxDest = 1.0 / FxDest;
	const double InvFyDest = 1.0 / FyDest;
	const float InvSizeX = 1.0f / (float)SizeXY.X;
	const float InvSizeY = 1.0f / (float)SizeXY.Y;

	for (int V = 0; V < SizeXY.Y; ++V)
	{
	   uint16* Row = &MipData[V * SizeXY.X * 2];
	   const double TargetY = (V - Cy) * InvFyDest;
	   const double TargetY2 = TargetY * TargetY;

	   for (int U = 0; U < SizeXY.X; ++U)
	   {
	   const double TargetX = (U - Cx) * InvFxDest;
	   const double TargetRadius = FMath::Sqrt(TargetX * TargetX + TargetY2);
	   
	   const double SourceRadius = SolveInverseDistortion(TargetRadius, K1, K2, K3);

	   const double Scale = (TargetRadius > 1e-6) ? (SourceRadius / TargetRadius) : 1.0;
		 
	   const float FinalU = (float)(TargetX * Scale * FxSource + Cx) * InvSizeX;
	   const float FinalV = (float)(TargetY * Scale * FySource + Cy) * InvSizeY;
	   
	   Row[U * 2 + 0] = FFloat16(FinalU).Encoded;
	   Row[U * 2 + 1] = FFloat16(FinalV).Encoded;
	   }
	}

	Mip.BulkData.Unlock();
	DistortionMapTexture->UpdateResource();
	if (PostProcessMaterialInstance)
	{
	   PostProcessMaterialInstance->SetTextureParameterValue(FName("DistortionMap"), DistortionMapTexture);
	   PostProcessMaterialInstance->SetScalarParameterValue(FName("CroppingFactor"), CroppingFactor);
	}
}

#if WITH_EDITOR
void UTempoCamera::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, LensParameters) || 
	   PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, DistortedFOV) ||
	   PropertyName == GET_MEMBER_NAME_CHECKED(UTempoCamera, SizeXY))
	{
	   UpdateLensParameters();
	}
}
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UTempoCamera::UpdateSceneCaptureContents(FSceneInterface* Scene)
#else
void UTempoCamera::UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder)
#endif
{
	if (!bDepthEnabled && !PendingDepthImageRequests.IsEmpty())
	{
	   SetDepthEnabled(true);
	}
	
	if (bDepthEnabled && PendingDepthImageRequests.IsEmpty())
	{
	   SetDepthEnabled(false);
	}
	
	UpdateLensParameters();

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	Super::UpdateSceneCaptureContents(Scene);
#else
	Super::UpdateSceneCaptureContents(Scene, SceneRenderBuilder);
#endif
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
	return FTempoCameraIntrinsics(SizeXY, DistortedFOV);
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

bool UTempoCamera::IsAwaitingRender()
{
	return IsNextReadAwaitingRender();
}

void UTempoCamera::OnRenderCompleted()
{
	ReadNextIfAvailable();
}

void UTempoCamera::BlockUntilMeasurementsReady() const
{
	BlockUntilNextReadComplete();
}

TOptional<TFuture<void>> UTempoCamera::SendMeasurements()
{
	TOptional<TFuture<void>> Future;

	if (TUniquePtr<FTextureRead> TextureRead = DequeueIfReadComplete())
	{
	   Future = DecodeAndRespond(MoveTemp(TextureRead));
	}

	return Future;
}

bool UTempoCamera::HasPendingRequests() const
{
	return !PendingColorImageRequests.IsEmpty() || !PendingLabelImageRequests.IsEmpty() || !PendingDepthImageRequests.IsEmpty() || !PendingBoundingBoxesRequests.IsEmpty();
}

FTextureRead* UTempoCamera::MakeTextureRead() const
{
	check(GetWorld());

	// Capture instance-to-semantic mapping at render time for bounding box requests
	TMap<uint8, uint8> InstanceToSemanticMap;
	if (UTempoActorLabeler* Labeler = GetWorld()->GetSubsystem<UTempoActorLabeler>())
	{
	   InstanceToSemanticMap = Labeler->GetInstanceToSemanticIdMap();
	}

	return bDepthEnabled ?
	   static_cast<FTextureRead*>(new TTextureRead<FCameraPixelWithDepth>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(), GetComponentTransform(), MinDepth, MaxDepth, MoveTemp(InstanceToSemanticMap))):
	   static_cast<FTextureRead*>(new TTextureRead<FCameraPixelNoDepth>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(), GetComponentTransform(), MoveTemp(InstanceToSemanticMap)));
}

TFuture<void> UTempoCamera::DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	const bool bSupportsDepth = TextureRead->GetType() == TEXT("WithDepth");

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

	PendingColorImageRequests.Empty();
	PendingLabelImageRequests.Empty();
	PendingBoundingBoxesRequests.Empty();
	if (bSupportsDepth)
	{
	   PendingDepthImageRequests.Empty();
	}
	
	return Future;
}

int32 UTempoCamera::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
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
	
	if (bDepthEnabled)
	{
	   RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	   PixelFormatOverride = EPixelFormat::PF_A16B16G16R16;

	   if (const TObjectPtr<UMaterialInterface> PostProcessMaterialWithDepth = GetDefault<UTempoSensorsSettings>()->GetCameraPostProcessMaterialWithDepth())
	   {
	   PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterialWithDepth.Get(), this);
	   
	   // Bind Distortion
	   if (DistortionMapTexture)
	   {
	   PostProcessMaterialInstance->SetTextureParameterValue(FName("DistortionMap"), DistortionMapTexture);
	   PostProcessMaterialInstance->SetScalarParameterValue(FName("CroppingFactor"), CroppingFactor);
	   }

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
	   if (const TObjectPtr<UMaterialInterface> PostProcessMaterialNoDepth = GetDefault<UTempoSensorsSettings>()->GetCameraPostProcessMaterialNoDepth())
	   {
	   PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterialNoDepth.Get(), this);
	   if (DistortionMapTexture)
	   {
	   PostProcessMaterialInstance->SetTextureParameterValue(FName("DistortionMap"), DistortionMapTexture);
	   PostProcessMaterialInstance->SetScalarParameterValue(FName("CroppingFactor"), CroppingFactor);
	   }
	   }
	   else
	   {
	   UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialWithDepth is not set in TempoSensors settings"));
	   }
	   
	   RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8; // Corresponds to PF_B8G8R8A8
	   PixelFormatOverride = EPixelFormat::PF_Unknown;
	}

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

	InitRenderTarget();
}