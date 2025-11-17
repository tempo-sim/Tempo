// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"

#include "TempoCameraModule.h"

#include "TempoSensorsSettings.h"

#include "TempoLabelTypes.h"

#include "TempoSensorsConstants.h"

#include "TempoActorLabeler.h"

#include "TempoLabels/Labels.pb.h"

#include "Engine/TextureRenderTarget2D.h"

// GPU compute shader infrastructure
#include "BoundingBoxComputeShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RHIGPUReadback.h"

// Intermediate data structure for 2D bounding box computation
struct FBoundingBox2D
{
	uint32 MinX = TNumericLimits<uint32>::Max();
	uint32 MinY = TNumericLimits<uint32>::Max();
	uint32 MaxX = 0;
	uint32 MaxY = 0;
	uint8 InstanceId = 0;

	bool IsValid() const { return MinX <= MaxX && MinY <= MaxY; }

	void Expand(uint32 X, uint32 Y)
	{
		MinX = FMath::Min(MinX, X);
		MinY = FMath::Min(MinY, Y);
		MaxX = FMath::Max(MaxX, X);
		MaxY = FMath::Max(MaxY, Y);
	}
};

/**
 * Compute 2D bounding boxes from label data.
 * Single-pass algorithm: scan all pixels, maintain min/max coords per instance ID.
 * @param LabelData Array of label values (one per pixel)
 * @param Width Image width in pixels
 * @param Height Image height in pixels
 * @return Array of valid bounding boxes
 */
static TArray<FBoundingBox2D> ComputeBoundingBoxesCPU(const TArray<uint8>& LabelData, uint32 Width, uint32 Height)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ComputeBoundingBoxesCPU);

	// Array indexed by instance ID (0-255)
	FBoundingBox2D Boxes[256];
	for (int i = 0; i < 256; i++)
	{
		Boxes[i].InstanceId = i;
	}

	// Single pass: scan all pixels
	for (uint32 Y = 0; Y < Height; Y++)
	{
		for (uint32 X = 0; X < Width; X++)
		{
			const uint8 InstanceId = LabelData[Y * Width + X];
			if (InstanceId > 0)  // 0 = unlabeled
			{
				Boxes[InstanceId].Expand(X, Y);
			}
		}
	}

	// Filter valid boxes
	TArray<FBoundingBox2D> ValidBoxes;
	ValidBoxes.Reserve(255);  // Scene can have up to 255 bounding boxes
	for (int i = 1; i < 256; i++)  // Skip 0 (unlabeled)
	{
		if (Boxes[i].IsValid())
		{
			ValidBoxes.Add(Boxes[i]);
		}
	}

	return ValidBoxes;
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
void RespondToBoundingBoxRequests(const TTextureRead<PixelType>* TextureRead, const TArray<FColorImageWithBoundingBoxesRequest>& Requests, float TransmissionTime)
{
	TempoCamera::ColorImageWithBoundingBoxes Response;
	if (!Requests.IsEmpty())
	{
		// Extract color image first
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeColor);
			TempoCamera::ColorImage* ColorImage = Response.mutable_color_image();
			ColorImage->set_width(TextureRead->ImageSize.X);
			ColorImage->set_height(TextureRead->ImageSize.Y);
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
			ColorImage->mutable_data()->assign(ImageData.begin(), ImageData.end());
			ColorImage->set_encoding(ColorEncodingToProto(Encoding));
			TextureRead->ExtractMeasurementHeader(TransmissionTime, ColorImage->mutable_header());
		}

		// Extract label data and compute bounding boxes
		TArray<FBoundingBox2D> BoundingBoxes;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeBoundingBoxes);

			// Check if we have GPU-computed bboxes available and synchronized
			// GPUBBoxSequenceId != 0 means we have valid GPU data from the correct frame
			if (TextureRead->GPUBBoxSequenceId != 0)
			{
				// Read GPU bboxes from CPU arrays
				TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraReadGPUBBoxes);

				// Extract valid bounding boxes from GPU data
				for (int32 InstanceId = 1; InstanceId < 256; InstanceId++)  // Skip 0 (unlabeled)
				{
					const FUintVector4& MinVec = TextureRead->BBoxMinData[InstanceId];
					const FUintVector4& MaxVec = TextureRead->BBoxMaxData[InstanceId];

					// Check if this instance has valid bbox (MinX <= MaxX)
					if (MinVec.X <= MaxVec.X && MinVec.Y <= MaxVec.Y)
					{
						FBoundingBox2D BBox;
						BBox.MinX = MinVec.X;
						BBox.MinY = MinVec.Y;
						BBox.MaxX = MaxVec.X;
						BBox.MaxY = MaxVec.Y;
						BBox.InstanceId = InstanceId;

						BoundingBoxes.Add(BBox);
					}
				}
			}
			else
			{
				// Fall back to CPU computation
				TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraComputeBBoxCPU);

				// Extract label data from pixels
				TArray<uint8> LabelData;
				LabelData.SetNumUninitialized(TextureRead->Image.Num());
				ParallelFor(TextureRead->Image.Num(), [&LabelData, &TextureRead](int32 Idx)
				{
					LabelData[Idx] = TextureRead->Image[Idx].Label();
				});

				BoundingBoxes = ComputeBoundingBoxesCPU(LabelData, TextureRead->ImageSize.X, TextureRead->ImageSize.Y);
			}
		}

		// Get instance-to-semantic mapping from UTempoActorLabeler
		UWorld* World = GEngine->GetWorldContexts()[0].World();
		UTempoActorLabeler* Labeler = World ? World->GetSubsystem<UTempoActorLabeler>() : nullptr;

		TMap<uint8, uint8> InstanceToSemanticMap;
		if (Labeler)
		{
			// Get the mapping synchronously (this is called from async task graph)
			TempoLabels::InstanceToSemanticIdMap Mapping;
			Labeler->GetInstanceToSemanticIdMap(TempoScripting::Empty(),
				TResponseDelegate<TempoLabels::InstanceToSemanticIdMap>::CreateLambda(
					[&Mapping](const TempoLabels::InstanceToSemanticIdMap& Response, const grpc::Status&)
					{
						Mapping = Response;
					}
				)
			);

			// Build lookup map
			for (const auto& Pair : Mapping.instance_semantic_id_pairs())
			{
				InstanceToSemanticMap.Add(Pair.instanceid(), Pair.semanticid());
			}
		}

		// Add bounding boxes to proto message
		for (const FBoundingBox2D& Box : BoundingBoxes)
		{
			TempoCamera::BoundingBox2D* BBoxProto = Response.add_bounding_boxes();
			BBoxProto->set_min_x(Box.MinX);
			BBoxProto->set_min_y(Box.MinY);
			BBoxProto->set_max_x(Box.MaxX);
			BBoxProto->set_max_y(Box.MaxY);
			BBoxProto->set_instance_id(Box.InstanceId);

			// Find semantic ID from mapping
			const uint8* SemanticId = InstanceToSemanticMap.Find(Box.InstanceId);
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

void TTextureRead<FCameraPixelNoDepth>::RespondToRequests(const TArray<FColorImageWithBoundingBoxesRequest>& Requests, float TransmissionTime) const
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

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FColorImageWithBoundingBoxesRequest>& Requests, float TransmissionTime) const
{
	RespondToBoundingBoxRequests(this, Requests, TransmissionTime);
}

UTempoCamera::UTempoCamera()
{
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE, EMeasurementType::COLOR_IMAGE_WITH_BBOXES};
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

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UTempoCamera::UpdateSceneCaptureContents(FSceneInterface* Scene)
#else
void UTempoCamera::UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder)
#endif
{
	if (!bDepthEnabled && !PendingDepthImageRequests.IsEmpty())
	{
		// If a client is requesting depth, start rendering it.
		SetDepthEnabled(true);
	}

	if (bDepthEnabled && PendingDepthImageRequests.IsEmpty())
	{
		// If no client is requesting depth, stop rendering it.
		SetDepthEnabled(false);
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	Super::UpdateSceneCaptureContents(Scene);
#else
	Super::UpdateSceneCaptureContents(Scene, SceneRenderBuilder);
#endif

	// Dispatch GPU bounding box computation if we have pending bbox requests
	if (!PendingColorImageWithBoundingBoxesRequests.IsEmpty() && TextureTarget)
	{
		FTextureRenderTargetResource* RenderTargetResource = TextureTarget->GameThread_GetRenderTargetResource();
		const FIntPoint ImageSize(TextureTarget->SizeX, TextureTarget->SizeY);
		const uint32 CurrentSequenceId = SequenceId;  // Capture sequence ID for this frame's GPU compute

		if (RenderTargetResource)
		{
			ENQUEUE_RENDER_COMMAND(ComputeBoundingBoxesGPU)(
				[this, RenderTargetResource, ImageSize, CurrentSequenceId](FRHICommandListImmediate& RHICmdList)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraComputeBBoxGPU);

					// Create RDG builder
					FRDGBuilder GraphBuilder(RHICmdList);

					// Register render target texture as external resource
					FTextureRHIRef RenderTargetTexture = RenderTargetResource->GetRenderTargetTexture();
					FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(
						CreateRenderTarget(RenderTargetTexture, TEXT("CameraRenderTarget"))
					);

					// Create or reuse output buffers for bounding boxes
					FRDGBufferDesc BBoxBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
						sizeof(FUintVector4),  // Size of uint4
						256  // One bbox per instance ID
					);

					FRDGBufferRef BBoxMinBuffer = GraphBuilder.CreateBuffer(BBoxBufferDesc, TEXT("BBoxMinBuffer"));
					FRDGBufferRef BBoxMaxBuffer = GraphBuilder.CreateBuffer(BBoxBufferDesc, TEXT("BBoxMaxBuffer"));

					// Clear buffers with initial values
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BBoxMinBuffer), 0xFFFFFFFF);  // Min: max uint
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BBoxMaxBuffer), 0x00000000);  // Max: 0

					// Get compute shader from global shader map
					TShaderMapRef<FBoundingBoxExtractionCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

					if (ComputeShader.IsValid())
					{
						// Setup shader parameters
						FBoundingBoxExtractionCS::FParameters* PassParameters =
							GraphBuilder.AllocParameters<FBoundingBoxExtractionCS::FParameters>();
						PassParameters->LabelTexture = SourceTexture;
						PassParameters->BBoxMinBuffer = GraphBuilder.CreateUAV(BBoxMinBuffer);
						PassParameters->BBoxMaxBuffer = GraphBuilder.CreateUAV(BBoxMaxBuffer);
						PassParameters->ImageWidth = ImageSize.X;
						PassParameters->ImageHeight = ImageSize.Y;

						// Calculate thread group dispatch size
						const uint32 NumGroupsX = FMath::DivideAndRoundUp((uint32)ImageSize.X, 16u);
						const uint32 NumGroupsY = FMath::DivideAndRoundUp((uint32)ImageSize.Y, 16u);

						// Add compute pass
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("BoundingBoxExtraction"),
							ComputeShader,
							PassParameters,
							FIntVector(NumGroupsX, NumGroupsY, 1)
						);

						// Extract buffers for later readback
						GraphBuilder.QueueBufferExtraction(BBoxMinBuffer, &BBoxMinBufferPooled);
						GraphBuilder.QueueBufferExtraction(BBoxMaxBuffer, &BBoxMaxBufferPooled);
					}
					else
					{
						UE_LOG(LogTempoCamera, Warning, TEXT("Bounding box compute shader not available, falling back to CPU"));
					}

					// Execute the render graph
					GraphBuilder.Execute();

					// Setup async readback after RDG execution
					if (BBoxMinBufferPooled.IsValid() && BBoxMaxBufferPooled.IsValid())
					{
						// Initialize readback objects if needed (reuse across frames)
						if (!BBoxMinReadback)
						{
							BBoxMinReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("BBoxMinReadback"));
						}
						if (!BBoxMaxReadback)
						{
							BBoxMaxReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("BBoxMaxReadback"));
						}

						// Enqueue async copy from GPU buffers to staging buffers (non-blocking)
						// FRHIGPUBufferReadback manages synchronization with internal fences
						const uint32 BufferSize = 256 * sizeof(FUintVector4);
						BBoxMinReadback->EnqueueCopy(RHICmdList, BBoxMinBufferPooled->GetRHI(), BufferSize);
						BBoxMaxReadback->EnqueueCopy(RHICmdList, BBoxMaxBufferPooled->GetRHI(), BufferSize);

						// Track which frame's GPU bbox data is in the readback pipeline
						LastGPUBBoxSequenceId.store(CurrentSequenceId, std::memory_order_relaxed);
					}
				});
		}
	}
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

void UTempoCamera::RequestMeasurement(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImageWithBoundingBoxes>& ResponseContinuation)
{
	PendingColorImageWithBoundingBoxesRequests.Add({ Request, ResponseContinuation});
}

FTempoCameraIntrinsics UTempoCamera::GetIntrinsics() const
{
	return FTempoCameraIntrinsics(SizeXY, FOVAngle);
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
	return !PendingColorImageRequests.IsEmpty() || !PendingLabelImageRequests.IsEmpty() || !PendingDepthImageRequests.IsEmpty() || !PendingColorImageWithBoundingBoxesRequests.IsEmpty();
}

FTextureRead* UTempoCamera::MakeTextureRead() const
{
	check(GetWorld());

	// Lambda to copy GPU bounding box buffers to TextureRead arrays (if ready and synchronized)
	auto CopyGPUBuffersIfReady = [this](auto* TextureRead)
	{
		const bool bBuffersExist = BBoxMinReadback && BBoxMaxReadback;
		const bool bMinReady = bBuffersExist && BBoxMinReadback->IsReady();
		const bool bMaxReady = bBuffersExist && BBoxMaxReadback->IsReady();
		const bool bSequenceMatches = (LastGPUBBoxSequenceId.load(std::memory_order_relaxed) == TextureRead->SequenceId);

		// Only copy GPU data if buffers are ready AND sequence IDs match (frame synchronized)
		if (bBuffersExist && bMinReady && bMaxReady && bSequenceMatches)
		{
			ENQUEUE_RENDER_COMMAND(CopyBBoxBuffers)(
				[TextureRead, MinReadback = BBoxMinReadback.Get(), MaxReadback = BBoxMaxReadback.Get(), SeqId = TextureRead->SequenceId](FRHICommandListImmediate& RHICmdList)
				{
					// Copy buffer data from staging buffers to CPU arrays
					TextureRead->BBoxMinData.SetNumUninitialized(256);
					TextureRead->BBoxMaxData.SetNumUninitialized(256);

					const uint32 BufferSize = 256 * sizeof(FUintVector4);

					// Lock staging buffers (safe because readback is complete)
					void* MinData = MinReadback->Lock(BufferSize);
					void* MaxData = MaxReadback->Lock(BufferSize);

					if (MinData && MaxData)
					{
						FMemory::Memcpy(TextureRead->BBoxMinData.GetData(), MinData, BufferSize);
						FMemory::Memcpy(TextureRead->BBoxMaxData.GetData(), MaxData, BufferSize);

						// Mark that this TextureRead has valid GPU bbox data from this sequence
						TextureRead->GPUBBoxSequenceId = SeqId;
					}

					MinReadback->Unlock();
					MaxReadback->Unlock();
				});
		}
		// If GPU data not ready or not synchronized, GPUBBoxSequenceId stays 0
		// RespondToBoundingBoxRequests will fall back to CPU computation
	};

	if (bDepthEnabled)
	{
		auto* TextureRead = new TTextureRead<FCameraPixelWithDepth>(
			SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(),
			GetComponentTransform(), MinDepth, MaxDepth);

		CopyGPUBuffersIfReady(TextureRead);
		return static_cast<FTextureRead*>(TextureRead);
	}
	else
	{
		auto* TextureRead = new TTextureRead<FCameraPixelNoDepth>(
			SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(),
			GetComponentTransform());

		CopyGPUBuffersIfReady(TextureRead);
		return static_cast<FTextureRead*>(TextureRead);
	}
}

TFuture<void> UTempoCamera::DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	// GPU bounding box data was already populated in MakeTextureRead() if ready and synchronized
	// If not ready/synchronized, GPUBBoxSequenceId = 0 and RespondToBoundingBoxRequests will use CPU fallback

	const bool bSupportsDepth = TextureRead->GetType() == TEXT("WithDepth");

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		TextureRead = MoveTemp(TextureRead),
		ColorImageRequests = PendingColorImageRequests,
		LabelImageRequests = PendingLabelImageRequests,
		DepthImageRequests = PendingDepthImageRequests,
		BoundingBoxRequests = PendingColorImageWithBoundingBoxesRequests,
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
	PendingColorImageWithBoundingBoxesRequests.Empty();
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

void UTempoCamera::BeginDestroy()
{
	// Flush rendering commands to ensure no pending GPU work references our resources
	FlushRenderingCommands();

	// Wait for any pending readbacks to complete before releasing
	if (BBoxMinReadback && !BBoxMinReadback->IsReady())
	{
		BBoxMinReadback->Wait();
	}
	if (BBoxMaxReadback && !BBoxMaxReadback->IsReady())
	{
		BBoxMaxReadback->Wait();
	}

	// Release GPU resources safely
	BBoxMinReadback.Reset();
	BBoxMaxReadback.Reset();
	BBoxMinBufferPooled.SafeRelease();
	BBoxMaxBufferPooled.SafeRelease();

	Super::BeginDestroy();
}
