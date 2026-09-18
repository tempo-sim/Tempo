// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoConversion.h"
#include "TempoSensors.h"
#include "Async/ParallelFor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "TempoLensModels.h"

#include "TempoSceneCaptureComponent2D.generated.h"

namespace TempoSensors
{
	class MeasurementHeader;
}

class FTextureRenderTargetResource;

// Most row bands the staging-surface copy is split into. Enough to spread a full camera frame
// across cores; small reads use fewer, see ComputeRowBands.
static constexpr int32 GTextureReadCopyRowBands = 32;

// Smallest copy worth its own band. Below this a memcpy finishes in about the time it takes to
// dispatch the task that would run it.
static constexpr int64 GTextureReadCopyMinBytesPerBand = 256 * 1024;

// Split NumRows rows into at most MaxBands bands of parallel work, halving the count while a band
// would carry less than MinWorkPerBand (in whatever unit WorkPerRow is measured in). The count is
// only ever halved, never derived from the work directly, so band seams stay at fixed fractions of
// the image height whatever the resolution.
inline int32 ComputeRowBands(int32 MaxBands, int32 NumRows, int64 WorkPerRow, int64 MinWorkPerBand)
{
	int32 NumBands = FMath::Clamp(NumRows, 1, MaxBands);
	const int64 TotalWork = WorkPerRow * NumRows;
	while (NumBands > 1 && TotalWork / NumBands < MinWorkPerBand)
	{
		NumBands /= 2;
	}
	return NumBands;
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

	// Map this read's staging texture and copy it to the CPU. Only reads what
	// CopyToStaging_RenderThread already put there.
	virtual void Read() = 0;

	// Copy the render target into StagingTexture and write RenderFence behind the copy. The only
	// place RenderFence is assigned, so this is the whole producer-side contract: every capture
	// path enqueues this once, behind the render that fills Source, and Read() maps the result.
	void TEMPOSENSORS_API CopyToStaging_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* Source);

	// Enqueue CopyToStaging_RenderThread as a render command. The command shares ownership of the
	// read, so a concurrent FTextureReadQueue::Empty() on the game thread cannot free it first.
	static void TEMPOSENSORS_API EnqueueStagingCopy(TSharedPtr<FTextureRead> Read, FTextureRenderTargetResource* Source);

	// Render thread. Whether Staging can hold an ImageSize image of PixelBytes-wide pixels: a
	// staging texture of a previous size or format must never be copied out of, since copying
	// ImageSize worth of pixels from a smaller surface over-reads it.
	static bool TEMPOSENSORS_API StagingMatches(const FRHITexture* Staging, const FIntPoint& ImageSize, int32 PixelBytes);

	// Render thread. Map Staging, waiting on Fence, and copy ImageSize rows of PixelBytes-wide pixels
	// out of it into Dst (tightly packed). Fence may be null when an earlier map has already waited
	// on the fence behind this copy; the map then waits for the GPU's pending work instead.
	static void TEMPOSENSORS_API CopyStagingSurface(FRHICommandListImmediate& RHICmdList, FRHITexture* Staging, FRHIGPUFence* Fence, uint8* Dst, const FIntPoint& ImageSize, int32 PixelBytes);

	// Render thread. Called by TTextureReadBase::Read once Image has been copied, before RenderFence
	// is released and the read is marked complete. A read that carries more than one image copies
	// the rest here, so a consumer never sees the read complete with part of it missing.
	virtual void ReadAdditional_RenderThread(FRHICommandListImmediate& RHICmdList) {}

	// The GPU fence behind the staging copy; signals once that copy has completed on the GPU.
	FGPUFenceRHIRef RenderFence;

	// The staging texture assigned to this read for GPU->CPU copy.
	FTextureRHIRef StagingTexture;

	void BlockUntilReadComplete() const
	{
		while (State != State::EReadComplete)
		{
			FPlatformProcess::Sleep(1e-4f);
		}
	}

	void TEMPOSENSORS_API ExtractMeasurementHeader(float TransmissionTime, TempoSensors::MeasurementHeader* MeasurementHeaderOut) const;

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

	virtual void Read() override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsTextureRead);
		check(IsInRenderingThread());
		check(StagingTexture.IsValid() && StagingTexture->IsValid());

		State = State::EReading;

		// Backstop against a staging texture whose per-pixel size or extent doesn't match this read.
		// AcquireNextStagingTexture waits on the init fence to prevent the known producer of such a
		// mismatch (an async staging recreate after a resize/format change), but if any path still
		// pairs a read with an under-sized staging texture, copying ImageSize worth of PixelType out
		// of it would over-read the mapped surface and crash in _platform_memmove. Skip instead: zero
		// the image and mark the read complete so consumers get a (blank) frame rather than corruption.
		if (!StagingMatches(StagingTexture, ImageSize, sizeof(PixelType)))
		{
			UE_LOG(LogTempoSensors, Warning,
				TEXT("Skipping texture read: staging texture (%dx%d, %dB/px) does not match read (%dx%d, %dB/px). Dropping frame."),
				StagingTexture->GetSizeXY().X, StagingTexture->GetSizeXY().Y, GPixelFormats[StagingTexture->GetFormat()].BlockBytes,
				ImageSize.X, ImageSize.Y, static_cast<int32>(sizeof(PixelType)));
			FMemory::Memzero(Image.GetData(), Image.Num() * sizeof(PixelType));
			State = State::EReadComplete;
			return;
		}

		// The fence is always set by the time we get here: CopyToStaging_RenderThread is enqueued
		// before the read joins the queue, so it has run before any later render command reaches
		// this one.
		if (!ensureMsgf(RenderFence.IsValid(), TEXT("Texture read had no render fence. Dropping frame.")))
		{
			FMemory::Memzero(Image.GetData(), Image.Num() * sizeof(PixelType));
			State = State::EReadComplete;
			return;
		}

		FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
		CopyStagingSurface(RHICmdList, StagingTexture, RenderFence, reinterpret_cast<uint8*>(Image.GetData()), ImageSize, sizeof(PixelType));

		ReadAdditional_RenderThread(RHICmdList);

		RenderFence.SafeRelease();

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

	// Shared ownership: the queue and any in-flight render-thread closures referencing the read
	// each hold a reference, so Empty()/Deactivate() can run on the game thread without UAF'ing a
	// closure that still needs to write the read's RenderFence.
	void Enqueue(TSharedPtr<FTextureRead> TextureRead)
	{
		FRWScopeLock_OnlyGTWrite WriteLock(Lock, SLT_Write);
		PendingTextureReads.Emplace(MoveTemp(TextureRead));
	}

	void Empty()
	{
		FRWScopeLock_OnlyGTWrite WriteLock(Lock, SLT_Write);
		PendingTextureReads.Empty();
	}

	bool IsAnyAwaitingRender() const
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		for (const TSharedPtr<FTextureRead>& TextureRead : PendingTextureReads)
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
	bool ReadAllAvailable(bool bBlock)
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		bool bAnyRead = false;
		for (const TSharedPtr<FTextureRead>& TextureRead : PendingTextureReads)
		{
			if (TextureRead->State != FTextureRead::State::EAwaitingRender || !TextureRead->RenderFence.IsValid())
			{
				continue;
			}
			if (bBlock)
			{
				while (!TextureRead->RenderFence->Poll())
				{
					FPlatformProcess::Sleep(1e-4f);
				}
			}
			else if (!TextureRead->RenderFence->Poll())
			{
				continue;
			}
			TextureRead->Read();
			bAnyRead = true;
		}
		return bAnyRead;
	}

	// Synchronously read back every awaiting read, without first polling RenderFence. On some RHIs
	// (e.g. Vulkan) that fence is not submitted to the GPU queue until end-of-frame, so it cannot be
	// polled to completion mid-tick. Read() instead hands the fence to RHIMapStagingSurface, which
	// submits the pending work and blocks until it completes. Used by the fixed-step blocking path.
	// Must run on the render thread.
	void ReadAllAwaitingBlocking()
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);

		// Dispatch anything still queued on the immediate list so every producer's staging copy is
		// on its way before the first map blocks on it. Once for the batch; ReadAllAvailable never
		// needs this, since a signaled fence means the copy has already run.
		FRHICommandListImmediate::Get().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

		for (const TSharedPtr<FTextureRead>& TextureRead : PendingTextureReads)
		{
			if (TextureRead->State != FTextureRead::State::EAwaitingRender)
			{
				continue;
			}
			TextureRead->Read();
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

	void EvictOldest()
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

	TSharedPtr<FTextureRead> DequeueIfReadComplete()
	{
		FRWScopeLock_OnlyGTWrite ReadLock(Lock, SLT_ReadOnly);
		if (!PendingTextureReads.IsEmpty() && PendingTextureReads[0]->State == FTextureRead::State::EReadComplete)
		{
			ReadLock.ReleaseReadOnlyLockAndAcquireWriteLock();
			TSharedPtr<FTextureRead> TextureRead = MoveTemp(PendingTextureReads[0]);
			PendingTextureReads.RemoveAt(0);
			return TextureRead;
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
	TArray<TSharedPtr<FTextureRead>> PendingTextureReads;
	mutable FRWLock Lock;
};

// A ring of CPU-readback staging textures, one per read that can be in flight. A sensor owns one
// per render target it reads back; the base capture component owns the ring for its own target.
struct TEMPOSENSORS_API FTempoStagingTextureRing
{
	// Game thread. (Re)create NumTextures textures of the given size and format on the render
	// thread. Waits for any previous creation to finish first, since that command writes Textures.
	void Allocate(const FString& NameBase, int32 NumTextures, int32 SizeX, int32 SizeY, EPixelFormat PixelFormat);

	// Game thread. Drop every texture, after any pending creation has finished.
	void Release();

	// Game thread. Block until a pending creation has completed, so a caller never pairs a read
	// with a texture of a previous size or format.
	void WaitForInit();

	// Game thread. The next texture in the ring; waits for a pending creation first.
	FTextureRHIRef AcquireNext();

	// Game thread. Whether the ring holds valid textures of the given format.
	bool IsValid(EPixelFormat PixelFormat);

	TArray<FTextureRHIRef> Textures;
	FCriticalSection Mutex;
	int32 NextIndex = 0;
	FRenderCommandFence InitFence;
};

UCLASS(Abstract)
class TEMPOSENSORS_API UTempoSceneCaptureComponent2D : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoSceneCaptureComponent2D();

	virtual void OnRegister() override;

	virtual void Activate(bool bReset) override;
	virtual void Deactivate() override;

	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder) override;

	// Expand FRayTracingScene's fixed-size readback rings on the render thread to work around the
	// engine's buffer overrun (see definition for detail). Idempotent and persistent for the FScene's
	// lifetime, so it's safe — and necessary — to call before *any* ray-tracing scene render this
	// component issues. The standard capture path calls it from UpdateSceneCaptureContents, but
	// components that render via a custom FSceneRenderer (e.g. the camera's multi-view tile path,
	// which bypasses UpdateSceneCaptureContents entirely) must call it themselves before rendering,
	// or the ring stays at the engine default of 4 and overruns immediately under many sensors.
	static void EnsureRayTracingReadbackBuffersExpanded(FSceneInterface* Scene);

	// Stop FRayTracingScene::EndFrame from deleting readback buffers that still have copies in flight
	// (see definition for detail). Unlike the ring expansion above this is NOT one-shot: it must be
	// called before every render this component issues that does not itself use ray tracing, since the
	// engine re-arms the release at the end of each EndFrame().
	static void PinRayTracingSceneUsedThisFrame(FSceneInterface* Scene);

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

	bool IsAnyReadAwaitingRender() const;
	void ReadNextIfAvailable();
	void BlockUntilNextReadComplete() const;
	TSharedPtr<FTextureRead> DequeueIfReadComplete();
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
	// PrincipalPoint is the optical center as a normalized offset from the output rect center
	// (X right, Y down, in fractions of width/height); (0,0) = centered. The pixel-to-normalized
	// conversion is taken about this point so an off-center principal point shifts the distortion
	// center accordingly. Must match the PrincipalPoint passed to the model's ComputeRenderConfig.
	static void FillDistortionMap(UTexture2D* DistortionMap, const FLensModel& Model,
		const FIntPoint& OutputSizeXY, double FOutput,
		const FIntPoint& RenderSizeXY,
		double TanLeft, double TanRight, double TanTop, double TanBottom,
		const FVector2D& PrincipalPoint = FVector2D::ZeroVector);

	// Gets the number of pending texture reads
	int32 NumPendingTextureReads() const { return TextureReadQueue.Num(); }

	// Returns the next staging texture from the ring buffer and advances the index.
	FTextureRHIRef AcquireNextStagingTexture();

protected:
	// (Re)allocates the staging-texture ring sized to the given dimensions and format. Waits for any
	// previous init render command to complete before reallocating. Derived classes whose readback
	// target is not the inherited TextureTarget call this from their own RT init path.
	void AllocateStagingTextures(int32 SizeX, int32 SizeY, EPixelFormat PixelFormat);

	// How many staging textures a ring needs: one per read that can be in flight, plus one.
	int32 GetNumStagingTextures() const;

	// Ring buffer of staging textures for GPU->CPU readback. Each in-flight FTextureRead
	// gets its own staging texture, preventing tearing when multiple frames are in flight.
	FTempoStagingTextureRing StagingRing;

private:
	// Starts or restarts the timer that calls MaybeCapture
	virtual void RestartCaptureTimer();

	// Capture a frame, if any client has requested one.
	virtual void MaybeCapture();

	// Our Queue of pending texture reads.
	FTextureReadQueue TextureReadQueue;

	FTimerHandle TimerHandle;
};
