// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include <atomic>
#include "TempoExposureHistogram.h"

class FRHICommandListImmediate;
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
// Readings are consumed at a fixed lag: each capture's metering first reads back every earlier
// capture's, waiting for the GPU if it has to, so a caller asking for the readings through a capture
// before the newest gets exactly those, however far the GPU and the render thread have got.
//
// Enqueue and ConsumeThrough are game thread calls; the readbacks are created, waited on and read on
// the render thread only. Render commands hold a reference, so the meter outlives any in flight.
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

	// Move out the measurements of every capture up to and including CaptureId not yet moved out,
	// oldest first. If the render thread hasn't read them all back yet, waits for it to. A capture that
	// was not metered, or whose histogram was empty, has none.
	void ConsumeThrough(int64 CaptureId, TArray<FMeasurement>& Out);

	// ComputeAverageLuminanceWithoutOutlier over a read-back histogram.
	static bool AverageLuminance(const uint32* Histogram, const FSettings& Settings, float& OutLuminance);

private:
	struct FPending
	{
		FRHIGPUBufferReadback* Readback = nullptr;
		FMeasurement Measurement;
		FSettings Settings;
	};

	// Read back the histograms of every capture up to and including ThroughCaptureId, in order,
	// waiting for the GPU to finish any that it hasn't. Render thread.
	void ReadBack_RenderThread(FRHICommandListImmediate& RHICmdList, int64 ThroughCaptureId, int64 NewestCaptureId);

	// Render thread only.
	TArray<FPending> Pending;
	TArray<FRHIGPUBufferReadback*> FreeReadbacks;

	// More in flight than this and a capture goes unmetered: the GPU is that far behind.
	static constexpr int32 MaxPending = 8;

	FCriticalSection CompletedLock;
	TArray<FMeasurement> Completed;
	// Every capture up to this one has been read back (or will never be). Written on the render thread.
	std::atomic<int64> ReadBackThroughCaptureId = 0;
};
