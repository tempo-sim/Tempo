// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "TempoExposureHistogram.h"

class FRHIGPUBufferReadback;
class FTextureRenderTargetResource;

// A camera's light meter: the engine's exposure histogram of each capture's stitched HDR image,
// read back and reduced to the engine's average scene luminance, tagged with the capture it measured.
//
// The engine's own readback (FSceneViewState's eye adaptation buffer) reports the newest result the
// GPU has finished, of whichever render that was, with nothing saying which; how many captures old
// it is depends on GPU and render thread load. A controller that divides it by the exposure of an
// assumed capture is wrong whenever the assumption is, and at a fast adaptation speed oscillates.
// Here every measurement carries the exposure its image was rendered with.
//
// Enqueue and Consume are game thread calls; the readbacks are created, polled and read on the
// render thread only. Render commands hold a reference, so the meter outlives any in flight.
class FTempoCameraExposureMeter : public TSharedFromThis<FTempoCameraExposureMeter, ESPMode::ThreadSafe>
{
public:
	struct FSettings
	{
		FTempoExposureHistogramParameters Histogram;
		// Fractions (0..1) of the histogram's weight below which and above which the average ignores
		// it (AutoExposureLowPercent / AutoExposureHighPercent, clamped as the engine clamps them).
		float LowFraction = 0.1f;
		float HighFraction = 0.9f;
	};

	struct FMeasurement
	{
		int64 CaptureId = 0;
		// What the image was rendered with, and when.
		float AppliedBias = 0.0f;
		double CaptureTime = 0.0;
		// The engine's AverageSceneLuminance of the image (ComputeEyeAdaptationExposure), as exposed.
		float MeasuredLuminance = 0.0f;
		// The newest capture enqueued when this measurement was read back.
		int64 ReadAtCaptureId = 0;
	};

	~FTempoCameraExposureMeter();

	// Queue a histogram of Target's current contents, rendered at AppliedBias. Render commands run in
	// order, so it measures whatever was drawn into Target before this call.
	void Enqueue(FTextureRenderTargetResource* Target, int64 CaptureId, float AppliedBias, double CaptureTime, const FSettings& Settings);

	// Move out every measurement read back since the last call, oldest first.
	void Consume(TArray<FMeasurement>& Out);

	// ComputeAverageLuminanceWithoutOutlier over a read-back histogram.
	static bool AverageLuminance(const uint32* Histogram, const FSettings& Settings, float& OutLuminance);

private:
	struct FPending
	{
		FRHIGPUBufferReadback* Readback = nullptr;
		FMeasurement Measurement;
		FSettings Settings;
	};

	// Read back every finished histogram, in order. Render thread.
	void Poll_RenderThread(int64 NewestCaptureId);

	// Render thread only.
	TArray<FPending> Pending;
	TArray<FRHIGPUBufferReadback*> FreeReadbacks;

	// More in flight than this and a capture goes unmetered: the GPU is that far behind.
	static constexpr int32 MaxPending = 8;

	FCriticalSection CompletedLock;
	TArray<FMeasurement> Completed;
};
