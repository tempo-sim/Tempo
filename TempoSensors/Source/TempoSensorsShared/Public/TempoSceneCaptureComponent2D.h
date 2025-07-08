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
		EReadComplete = 2
	};
	
	FTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn, const FString& SensorNameIn)
		: ImageSize(ImageSizeIn), SequenceId(SequenceIdIn), CaptureTime(CaptureTimeIn), OwnerName(OwnerNameIn), SensorName(SensorNameIn), State(State::EAwaitingRender) {}

	virtual ~FTextureRead() {}

	virtual FName GetType() const = 0;

	virtual void Read(const FRenderTarget* RenderTarget, const FTextureRHIRef& TextureRHICopy) = 0;

	void BlockUntilReadComplete() const
	{
		while (State != State::EReadComplete)
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

	virtual void Read(const FRenderTarget* RenderTarget, const FTextureRHIRef& TextureRHICopy) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsTextureRead);
		check(IsInRenderingThread());

		State = State::EReading;

		FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();

		// Then, transition our TextureTarget to be copyable.
		RHICmdList.Transition(FRHITransitionInfo(RenderTarget->GetRenderTargetTexture(), ERHIAccess::Unknown, ERHIAccess::CopySrc));

		// Then, copy our TextureTarget to another that can be mapped and read by the CPU.
		RHICmdList.CopyTexture(RenderTarget->GetRenderTargetTexture(), TextureRHICopy, FRHICopyTextureInfo());

		// Write a GPU fence to wait for the above copy to complete before reading the data.
		const FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("TempoCameraTextureRead"));
		RHICmdList.WriteGPUFence(Fence);
		RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

		// Lastly, read the raw data from the copied TextureTarget on the CPU.
		void* OutBuffer;
		int32 SurfaceWidth, SurfaceHeight;
		GDynamicRHI->RHIMapStagingSurface(TextureRHICopy, Fence, OutBuffer, SurfaceWidth, SurfaceHeight, RHICmdList.GetGPUMask().ToIndex());
		FMemory::Memcpy(Image.GetData(), OutBuffer, SurfaceWidth * SurfaceHeight * sizeof(PixelType));
		RHICmdList.UnmapStagingSurface(TextureRHICopy);
		
		State = State::EReadComplete;
	}
	
	TArray<PixelType> Image;
};

template <typename PixelType>
struct TTextureRead : TTextureReadBase<PixelType>
{
	using TTextureReadBase<PixelType>::TTextureReadBase;
};

// An FRWScopeLock that only allows write locks on the game thread, thereby eliminating the concern
// that upgrading to a write lock requires releasing the read lock.
class FRWScopeLock_OnlyGTWrite : FRWScopeLock
{
public:
	UE_NODISCARD_CTOR explicit FRWScopeLock_OnlyGTWrite(FRWLock& InLockObject,FRWScopeLockType InLockType)
		: FRWScopeLock(InLockObject, InLockType)
	{
		if (InLockType != SLT_ReadOnly)
		{
			check(IsInGameThread());
		}
	}

	void ReleaseReadOnlyLockAndAcquireWriteLock()
	{
		check(IsInGameThread());
		ReleaseReadOnlyLockAndAcquireWriteLock_USE_WITH_CAUTION();
	}
};

struct FTextureReadQueue
{
	int32 Num() const
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		return PendingTextureReads.Num();
	}
	
	void Enqueue(FTextureRead* TextureRead)
	{
		FRWScopeLock_OnlyGTWrite WriteLock(Lock, SLT_Write);
		PendingTextureReads.Emplace(TextureRead);
	}

	void Empty()
	{
		FRWScopeLock_OnlyGTWrite WriteLock(Lock, SLT_Write);
		PendingTextureReads.Empty();
	}

	bool IsNextAwaitingRender() const
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		for (const TUniquePtr<FTextureRead>& TextureRead : PendingTextureReads)
		{
			if (TextureRead->State == FTextureRead::State::EAwaitingRender)
			{
				return true;
			}
		}
		return false;
	}

	void ReadNext(const FRenderTarget* RenderTarget, const FTextureRHIRef& TextureRHICopy)
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		if (!PendingTextureReads.IsEmpty())
		{
			for (const TUniquePtr<FTextureRead>& TextureRead : PendingTextureReads)
			{
				if (TextureRead->State == FTextureRead::State::EAwaitingRender)
				{
					TextureRead->Read(RenderTarget, TextureRHICopy);
				}
			}
		}
	}

	void SkipNext()
	{
		FRWScopeLock_OnlyGTWrite WriteLock(Lock, SLT_Write);
		if (!PendingTextureReads.IsEmpty())
		{
			PendingTextureReads.RemoveAt(0);
		}
	}

	void BlockUntilNextReadComplete() const
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		if (!PendingTextureReads.IsEmpty())
		{
			PendingTextureReads[0]->BlockUntilReadComplete();
		}
	}

	TUniquePtr<FTextureRead> DequeueIfReadComplete()
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		if (!PendingTextureReads.IsEmpty() && PendingTextureReads[0]->State == FTextureRead::State::EReadComplete)
		{
			ReadLock.ReleaseReadOnlyLockAndAcquireWriteLock();
			TUniquePtr<FTextureRead> TextureRead(PendingTextureReads[0].Release());
			PendingTextureReads.RemoveAt(0);
			return MoveTemp(TextureRead);
		}
		return nullptr;
	}

private:
	TArray<TUniquePtr<FTextureRead>> PendingTextureReads;
	mutable FRWLock Lock;
};

UCLASS(Abstract)
class TEMPOSENSORSSHARED_API UTempoSceneCaptureComponent2D : public USceneCaptureComponent2D, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	UTempoSceneCaptureComponent2D();

	virtual void BeginPlay() override;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;
#else
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder) override;
#endif

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
	// Starts or restarts the timer that calls MaybeCapture
	void RestartCaptureTimer();

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
