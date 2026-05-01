// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoConversion.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "TempoDistortionModels.h"

#include "TempoSceneCaptureComponent2D.generated.h"

namespace TempoSensorsShared
{
	class MeasurementHeader;
}

struct FTextureRead
{
	enum class State : uint8
	{
		EAwaitingRender = 0,
		EReading = 1,
		EReadComplete = 2
	};
	
	FTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
		const FString& SensorNameIn, const FTransform& SensorTransformIn)
		: ImageSize(ImageSizeIn), SequenceId(SequenceIdIn), CaptureTime(CaptureTimeIn), OwnerName(OwnerNameIn),
			SensorName(SensorNameIn), SensorTransform(SensorTransformIn), State(State::EAwaitingRender) {}

	virtual ~FTextureRead() {}

	virtual FName GetType() const = 0;

	virtual void Read(const FRenderTarget* RenderTarget) = 0;

	// The GPU fence indicating our render has completed. Set during UpdateSceneCaptureContents.
	FGPUFenceRHIRef RenderFence;

	// The staging texture assigned to this read for GPU->CPU copy.
	FTextureRHIRef StagingTexture;

	void BlockUntilReadComplete() const
	{
		while (State != State::EReadComplete)
		{
			FPlatformProcess::Sleep(1e-4);
		}
	}

	void TEMPOSENSORSSHARED_API ExtractMeasurementHeader(float TransmissionTime, TempoSensorsShared::MeasurementHeader* MeasurementHeaderOut) const;

	FIntPoint ImageSize;
	int32 SequenceId;
	double CaptureTime;
	const FString OwnerName;
	const FString SensorName;
	const FTransform SensorTransform;
	TAtomic<State> State;
};

template <typename PixelType>
struct TTextureReadBase : FTextureRead
{
	TTextureReadBase(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
		const FString& SensorNameIn, const FTransform& SensorTransformIn)
		: FTextureRead(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn, SensorTransformIn)
	{
		Image.SetNumUninitialized(ImageSize.X * ImageSize.Y);
	}

	virtual void Read(const FRenderTarget* RenderTarget) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsTextureRead);
		check(IsInRenderingThread());
		check(StagingTexture.IsValid() && StagingTexture->IsValid());

		State = State::EReading;

		FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();

		// Then, transition our TextureTarget to be copyable.
		RHICmdList.Transition(FRHITransitionInfo(RenderTarget->GetRenderTargetTexture(), ERHIAccess::Unknown, ERHIAccess::CopySrc));

		// Then, copy our TextureTarget to this read's dedicated staging texture.
		RHICmdList.CopyTexture(RenderTarget->GetRenderTargetTexture(), StagingTexture, FRHICopyTextureInfo());

		// Write a GPU fence to wait for the above copy to complete before reading the data.
		const FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("TempoCameraTextureRead"));
		RHICmdList.WriteGPUFence(Fence);
		RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

		// Lastly, read the raw data from the copied TextureTarget on the CPU.
		// Note: SurfaceWidth may be larger than ImageSize.X due to GPU row alignment padding.
		// We must copy row-by-row to account for this pitch difference.
		void* OutBuffer;
		int32 SurfaceWidth, SurfaceHeight;
		GDynamicRHI->RHIMapStagingSurface(StagingTexture, Fence, OutBuffer, SurfaceWidth, SurfaceHeight, RHICmdList.GetGPUMask().ToIndex());
		const int32 SrcPitch = SurfaceWidth * sizeof(PixelType);
		const int32 DstPitch = ImageSize.X * sizeof(PixelType);
		if (SurfaceWidth == ImageSize.X)
		{
			FMemory::Memcpy(Image.GetData(), OutBuffer, DstPitch * ImageSize.Y);
		}
		else
		{
			const uint8* SrcRow = static_cast<const uint8*>(OutBuffer);
			uint8* DstRow = reinterpret_cast<uint8*>(Image.GetData());
			for (int32 Row = 0; Row < ImageSize.Y; ++Row)
			{
				FMemory::Memcpy(DstRow, SrcRow, DstPitch);
				SrcRow += SrcPitch;
				DstRow += DstPitch;
			}
		}
		RHICmdList.UnmapStagingSurface(StagingTexture);

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

	// Poll render fences on all awaiting reads and initiate readback for any that are ready.
	// If bBlock is true, spin-waits on each fence. Returns true if any reads were initiated.
	bool ReadAllAvailable(const FRenderTarget* RenderTarget, bool bBlock)
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		bool bAnyRead = false;
		for (const TUniquePtr<FTextureRead>& TextureRead : PendingTextureReads)
		{
			if (TextureRead->State != FTextureRead::State::EAwaitingRender || !TextureRead->RenderFence.IsValid())
			{
				continue;
			}
			if (bBlock)
			{
				while (!TextureRead->RenderFence->Poll())
				{
					FPlatformProcess::Sleep(1e-4);
				}
			}
			else if (!TextureRead->RenderFence->Poll())
			{
				continue;
			}
			TextureRead->RenderFence.SafeRelease();
			TextureRead->Read(RenderTarget);
			bAnyRead = true;
		}
		return bAnyRead;
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

	TOptional<int32> SequenceIdOfNextCompleteRead() const
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		if (!PendingTextureReads.IsEmpty() && PendingTextureReads[0]->State == FTextureRead::State::EReadComplete)
		{
			return PendingTextureReads[0]->SequenceId;
		}
		return TOptional<int32>();
	}

	bool NextReadComplete() const
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		return !PendingTextureReads.IsEmpty() && PendingTextureReads[0]->State == FTextureRead::State::EReadComplete;
	}

private:
	TArray<TUniquePtr<FTextureRead>> PendingTextureReads;
	mutable FRWLock Lock;
};

UCLASS(Abstract)
class TEMPOSENSORSSHARED_API UTempoSceneCaptureComponent2D : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoSceneCaptureComponent2D();

	virtual void OnRegister() override;

	virtual void Activate(bool bReset) override;
	virtual void Deactivate() override;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;
#else
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder) override;
#endif

protected:
	// Derived components must override this to return whether they have pending requests.
	virtual bool HasPendingRequests() const PURE_VIRTUAL(UTempoSceneCaptureComponent2D::HasPendingRequests, return false; );

	// Derived components must override this to create new texture reads, based on their current settings, to be enqueued.
	virtual FTextureRead* MakeTextureRead() const PURE_VIRTUAL(UTempoSceneCaptureComponent2D::MakeTextureRead, return nullptr; );

	// Derived components may override this to limit the size of the texture queue.
	virtual int32 GetMaxTextureQueueSize() const { return -1; }

	// When false, this component does not allocate staging textures, does not create FTextureReads,
	// does not enqueue render fences, and does not increment SequenceId in UpdateSceneCaptureContents.
	// An outer owner is expected to stitch this component's render target into a shared RT and issue
	// a single readback command for the whole sensor. Derived tile components should return false.
	virtual bool ShouldManageOwnReadback() const { return true; }

	// When false, this component does not start a capture timer on Activate. An outer owner drives
	// CaptureScene() directly when it has captured all its tiles. Derived tile components should return false.
	virtual bool ShouldManageOwnTimer() const { return true; }

	bool IsNextReadAwaitingRender() const;
	void ReadNextIfAvailable();
	void BlockUntilNextReadComplete() const;
	TUniquePtr<FTextureRead> DequeueIfReadComplete();
	TOptional<int32> SequenceIDOfNextCompleteRead() const;
	bool NextReadComplete() const;

	// Derived components may set this to use a non-default render target format.
	UPROPERTY(VisibleAnywhere, Category = "Tempo")
	TEnumAsByte<ETextureRenderTargetFormat> RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;

	// Derived components may set this to use a non-default pixel format.
	UPROPERTY(VisibleAnywhere, Category = "Tempo")
	TEnumAsByte<EPixelFormat> PixelFormatOverride = EPixelFormat::PF_Unknown;

	// The rate in Hz this sensor updates at.
	UPROPERTY(EditAnywhere, Category = "Tempo")
	float RateHz = 10.0;

	// Capture resolution.
	UPROPERTY(EditAnywhere, Category = "Tempo")
	FIntPoint SizeXY = FIntPoint(960, 540);

	// Monotonically increasing counter of frames captured.
	UPROPERTY(VisibleAnywhere, Category = "Tempo")
	int32 SequenceId = 0;

	// Initialize our RenderTarget and TextureRHICopy with the current settings.
	virtual void InitRenderTarget();

	// Override this to initialize the distortion map texture with a projection-specific mapping.
	virtual void InitDistortionMap() {}

	// Create or resize the distortion map texture to the given size. Allocates into OutTexture.
	static void CreateOrResizeDistortionMapTexture(UTexture2D*& OutTexture, const FIntPoint& TextureSizeXY);

	// Apply the distortion map texture to the given material instance.
	static void ApplyDistortionMapToMaterial(UMaterialInstanceDynamic* MaterialInstance, UTexture2D* DistortionMap);

	// Fill the distortion map texture using the given distortion model.
	// OutputSizeXY / FOutput describe the output (distorted) image (loop bounds + normalization).
	// RenderSizeXY plus the signed Tan{Left,Right,Top,Bottom} bounds describe the render
	// (perspective) image — the UV [0,1] range maps linearly across [TanLeft, TanRight] and
	// [TanTop, TanBottom]. Off-axis frustums collapse the wasted regions of a symmetric frustum
	// and write the same UV semantics; old call sites get unchanged behavior by passing
	// TanLeft = -TanRight and TanTop = -TanBottom.
	static void FillDistortionMap(UTexture2D* DistortionMap, const FDistortionModel& Model,
		const FIntPoint& OutputSizeXY, double FOutput,
		const FIntPoint& RenderSizeXY,
		double TanLeft, double TanRight, double TanTop, double TanBottom);

	// Gets the number of pending texture reads
	int32 NumPendingTextureReads() const { return TextureReadQueue.Num(); }

	// Returns the next staging texture from the ring buffer and advances the index.
	FTextureRHIRef AcquireNextStagingTexture();

protected:
	// (Re)allocates the staging-texture ring sized to the given dimensions and format. Waits for any
	// previous init render command to complete before reallocating. Derived classes whose readback
	// target is not the inherited TextureTarget call this from their own RT init path.
	void AllocateStagingTextures(int32 SizeX, int32 SizeY, EPixelFormat PixelFormat);

	// Ring buffer of staging textures for GPU->CPU readback. Each in-flight FTextureRead
	// gets its own staging texture, preventing tearing when multiple frames are in flight.
	TArray<FTextureRHIRef> StagingTextures;
	FCriticalSection StagingTexturesMutex;
	int32 NextStagingIndex = 0;

	// A fence to indicate that our staging textures have been initialized. Should only be accessed from the Game thread.
	FRenderCommandFence TextureInitFence;

private:
	// Starts or restarts the timer that calls MaybeCapture
	virtual void RestartCaptureTimer();

	// Capture a frame, if any client has requested one.
	virtual void MaybeCapture();

	// Our Queue of pending texture reads.
	FTextureReadQueue TextureReadQueue;

	FTimerHandle TimerHandle;
};
