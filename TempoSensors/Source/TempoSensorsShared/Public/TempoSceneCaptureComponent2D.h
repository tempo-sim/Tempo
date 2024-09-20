// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSensorInterface.h"

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

#include "TempoSceneCaptureComponent2D.generated.h"

struct FTextureRead
{
	enum class State : uint8
	{
		EAwaitingRender = 0,
		EReading = 1,
		EReadyToSend = 2
	};
	
	FTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn, const FString& SensorNameIn)
		: ImageSize(ImageSizeIn), SequenceId(SequenceIdIn), CaptureTime(CaptureTimeIn), OwnerName(OwnerNameIn), SensorName(SensorNameIn), State(State::EAwaitingRender) {}

	virtual ~FTextureRead() {}

	virtual FName GetType() const = 0;

	virtual void BeginRead(const FRenderTarget* RenderTarget, const FTextureRHIRef& TextureRHICopy) = 0;

	void BlockUntilReadyToSend() const
	{
		while (State != State::EReadyToSend)
		{
			FPlatformProcess::Sleep(1e-4);
		}
	}
	
	FIntPoint ImageSize;
	int32 SequenceId;
	double CaptureTime;
	const FString OwnerName;
	const FString SensorName;
	TAtomic<State> State;
};

template <typename PixelType>
struct TTextureReadBase : FTextureRead
{
	TTextureReadBase(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn, const FString& SensorNameIn)
		   : FTextureRead(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn)
	{
		Image.SetNumUninitialized(ImageSize.X * ImageSize.Y);
	}

	virtual void BeginRead(const FRenderTarget* RenderTarget, const FTextureRHIRef& TextureRHICopy) override
	{
		State = State::EReading;

		FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();

		// Then, transition our TextureTarget to be copyable.
		RHICmdList.Transition(FRHITransitionInfo(RenderTarget->GetRenderTargetTexture(), ERHIAccess::Unknown, ERHIAccess::CopySrc));

		// Then, copy our TextureTarget to another that can be mapped and read by the CPU.
		RHICmdList.CopyTexture(RenderTarget->GetRenderTargetTexture(), TextureRHICopy, FRHICopyTextureInfo());

		// Write a GPU fence to wait for the above copy to complete before reading the data.
		const FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("TempoCameraTextureRead"));
		RHICmdList.WriteGPUFence(Fence);

		// Lastly, read the raw data from the copied TextureTarget on the CPU.
		void* OutBuffer;
		int32 SurfaceWidth, SurfaceHeight;
		GDynamicRHI->RHIMapStagingSurface(TextureRHICopy, Fence, OutBuffer, SurfaceWidth, SurfaceHeight, RHICmdList.GetGPUMask().ToIndex());
		FMemory::Memcpy(Image.GetData(), OutBuffer, SurfaceWidth * SurfaceHeight * sizeof(PixelType));
		RHICmdList.UnmapStagingSurface(TextureRHICopy);
		
		State = State::EReadyToSend;
	}
	
	TArray<PixelType> Image;
};

template <typename PixelType>
struct TTextureRead : TTextureReadBase<PixelType>
{
	using TTextureReadBase<PixelType>::TTextureReadBase;
};

struct FTextureReadQueue
{
	int32 GetNum() const
	{
		FScopeLock Lock(&Mutex);
		return PendingTextureReads.Num();
	}
	
	void Enqueue(FTextureRead* TextureRead)
	{
		FScopeLock Lock(&Mutex);
		PendingTextureReads.Emplace(TextureRead);
	}

	void Empty()
	{
		FScopeLock Lock(&Mutex);
		PendingTextureReads.Empty();
	}

	bool IsNextAwaitingRender() const
	{
		FScopeLock Lock(&Mutex);
		return !PendingTextureReads.IsEmpty() && PendingTextureReads[0]->State == FTextureRead::State::EAwaitingRender;
	}

	void BeginNextRead(const FRenderTarget* RenderTarget, const FTextureRHIRef& TextureRHICopy)
	{
		FScopeLock Lock(&Mutex);
		if (!PendingTextureReads.IsEmpty())
		{
			check(PendingTextureReads[0]->State == FTextureRead::State::EAwaitingRender);
			PendingTextureReads[0]->BeginRead(RenderTarget, TextureRHICopy);
		}
	}

	void SkipNext()
	{
		FScopeLock Lock(&Mutex);
		if (!PendingTextureReads.IsEmpty())
		{
			PendingTextureReads.RemoveAt(0);
		}
	}

	void BlockUntilNextReadyToSend() const
	{
		// Is this safe?
		// FScopeLock Lock(&Mutex);
		if (!PendingTextureReads.IsEmpty())
		{
			PendingTextureReads[0]->BlockUntilReadyToSend();
		}
	}

	TUniquePtr<FTextureRead> DequeueIfReadyToSend()
	{
		FScopeLock Lock(&Mutex);
		if (!PendingTextureReads.IsEmpty() && PendingTextureReads[0]->State == FTextureRead::State::EReadyToSend)
		{
			TUniquePtr<FTextureRead> TextureRead(PendingTextureReads[0].Release());
			PendingTextureReads.RemoveAt(0);
			return MoveTemp(TextureRead);
		}
		return nullptr;
	}

private:
	TArray<TUniquePtr<FTextureRead>> PendingTextureReads;
	mutable FCriticalSection Mutex;
};

UCLASS(Abstract)
class TEMPOSENSORSSHARED_API UTempoSceneCaptureComponent2D : public USceneCaptureComponent2D, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	UTempoSceneCaptureComponent2D();

	virtual void BeginPlay() override;

	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

	// Begin ITempoSensorInterface
	virtual FString GetOwnerName() const override;
	virtual FString GetSensorName() const override;
	virtual float GetRate() const override { return RateHz; }
	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const override { return MeasurementTypes; }
	virtual bool IsAwaitingRender() override;
	virtual void OnRenderCompleted() override;
	virtual void BlockUntilMeasurementsReady() const override;
	virtual TOptional<TFuture<void>> SendMeasurements() override;
	// End ITempoSensorInterface

protected:
	// Derived components must override this to return whether they have pending requests.
	virtual bool HasPendingRequests() const PURE_VIRTUAL(UTempoSceneCaptureComponent2D::HasPendingRequests, return false; );

	// Derived components must override this to create new texture reads, based on their current settings, to be enqueued.
	virtual FTextureRead* MakeTextureRead() const PURE_VIRTUAL(UTempoSceneCaptureComponent2D::MakeTextureRead, return nullptr; );

	// Derived components must override this to decode a completed texture read and use it to respond to pending requests.
	virtual TFuture<void> DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead) PURE_VIRTUAL(UTempoSceneCaptureComponent2D::DecodeAndRespond, return TFuture<void>(); );

	// Derived components may override this to limit the size of the texture queue.
	virtual int32 GetMaxTextureQueueSize() const { return -1; }

	// Derived components may set this to use a non-default render target format.
	UPROPERTY(VisibleAnywhere)
	TEnumAsByte<ETextureRenderTargetFormat> RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;

	// Derived components may set this to use a non-default pixel format.
	UPROPERTY(VisibleAnywhere)
	TEnumAsByte<EPixelFormat> PixelFormatOverride = EPixelFormat::PF_Unknown;

	// The rate in Hz this sensor updates at.
	UPROPERTY(EditAnywhere)
	float RateHz = 10.0;

	// The measurement types supported. Should be set in constructor of derived classes.
	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	// Capture resolution.
	UPROPERTY(EditAnywhere)
	FIntPoint SizeXY = FIntPoint(960, 540);

	// Monotonically increasing counter of frames captured.
	UPROPERTY(VisibleAnywhere)
	int32 SequenceId = 0;

	// Initialize our RenderTarget and TextureRHICopy with the current settings.
	void InitRenderTarget();

private:
	// Capture a frame, if any client has requested one.
	void MaybeCapture();

	// Our Queue of pending texture reads.
	FTextureReadQueue TextureReadQueue;

	// A fence to indicate that our render textures have been initialized. Should only be accessed from the Game thread.
	FRenderCommandFence TextureInitFence;

	// A GPU fence to indicate that our latest render has completed. Should only be accessed from the Render thread.
	FGPUFenceRHIRef RenderFence;

	// We must copy our TextureTarget's resource here before reading it on the CPU
	// because USceneCaptureComponent's RenderTarget is not set up to do so.
	FTextureRHIRef TextureRHICopy;

	FTimerHandle TimerHandle;
};
