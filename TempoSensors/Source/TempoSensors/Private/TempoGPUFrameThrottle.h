// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "RHIFwd.h"
#include "RHIResources.h"

#include <atomic>

class FRHICommandListImmediate;

// Bounds how far the GPU may lag behind the render thread, the way a viewport present does for a
// windowed process.
//
// The engine's only CPU-to-GPU throttle is the present: FVulkanViewport::Present waits for the
// previous frame's GPU work before the frame ends. A process rendering with -RenderOffScreen has no
// RHI viewport at all (Slate draws into a plain texture), so nothing bounds the lag; the render thread
// hands the RHI thread frame after frame and the GPU gets to them whenever it can. Several Vulkan RHI
// heuristics assume the lag is bounded: uniform-buffer memory (FMemoryManager::FreeUniformBuffer),
// descriptor pools and buffer-pool pages are recycled a fixed number of *render-thread* frames
// (r.Vulkan.NumFramesToWaitForResourceDelete, 2) after release, with no GPU sync point, unlike
// buffers and images, which go through the end-of-pipe deferred deletion queue. Once the GPU is three
// or more frames behind, a queued draw reads memory that has been handed to someone else: on NVIDIA a
// VK_ERROR_DEVICE_LOST with a READ_INVALID page fault, which the fleet saw in the camera's stitch
// merge draw (each Canvas flush creates and destroys a vertex factory whose uniform buffer carries the
// bindless vertex-fetch indices), or a hang where the recycled memory merely held garbage.
//
// With pipelined rendering nothing on the Tempo side waits for the GPU either, and in FixedStep mode
// the engine ticks at wall rate while the GPU is saturated, so the lag grows until the sensors' read
// queues fill. This class restores the present's bound: every render-thread frame writes a GPU fence,
// and before a frame ends the fence written MaxFramesInFlight frames earlier must have signaled. The
// wait costs nothing while the GPU keeps up and stalls the render thread only when it would otherwise
// be issuing work against memory about to be recycled. Whether or not the wait is enabled, the lag is
// measured every frame and summarized to the log, so a fleet log says how far behind the GPU ran.
class FTempoGPUFrameThrottle
{
public:
	// The most recent frames whose completion is tracked. Lag is measured against these, and the
	// bound may not exceed it. Generous because a useful bound counts the cheap frames between two
	// captures: a sensor at 5 Hz in a sim stepping at 100 Hz has nineteen of them, and a bound below
	// that idles the GPU between captures.
	static constexpr int32 MeasureWindow = 64;

	// Which frame a frame that is ending must see complete before it ends: the one MaxFramesInFlight
	// frames earlier, once that many have been issued. Pure, for the unit test.
	static TOptional<uint64> FrameToWaitFor(uint64 Frame, int32 MaxFramesInFlight);

	// How many of the frames before Frame are still outstanding: frames complete in order, so it is
	// the run of incomplete frames ending at Frame - 1, up to Window long. Pure, for the unit test.
	static int32 MeasureLag(uint64 Frame, int32 Window, TFunctionRef<bool(uint64)> IsFrameComplete);

	// The ring slot a frame's fence lives in. The ring holds MeasureWindow earlier frames plus the
	// frame being written, so a slot is only overwritten once its frame has left the window.
	static constexpr int32 NumSlots() { return MeasureWindow + 1; }
	static constexpr int32 SlotForFrame(uint64 Frame) { return static_cast<int32>(Frame % NumSlots()); }

	// Game thread. Snapshot the settings; the render thread reads them at the next frame end.
	void Configure(bool bLimitFramesInFlight, int32 MaxFramesInFlight);

	// Render thread, at the end of the frame (OnEndFrameRT), before the frame's RHI cleanup: measure
	// the lag, wait it down to the bound if enabled, then write this frame's fence.
	void OnEndFrameRT(FRHICommandListImmediate& RHICmdList);

	// Render thread. Drop the fences.
	void Release();

	// Render thread. How many times the bound has been enforced with an actual wait.
	uint64 GetNumWaits() const { return NumWaits; }

private:
	struct FSlot
	{
		uint64 Frame = 0;
		FGPUFenceRHIRef Fence;
		// Completes once the RHI thread has translated the fence write, which is when the fence can
		// be waited on at all (a Vulkan fence has no sync point before that).
		FGraphEventRef RHIThreadFence;
	};

	bool IsFrameComplete(uint64 Frame) const;
	void WaitForFrame(FRHICommandListImmediate& RHICmdList, uint64 Frame);
	void Report(int32 MaxFramesInFlight, bool bLimitFramesInFlight);

	TArray<FSlot> Slots;
	uint64 NextFrame = 0;

	std::atomic<bool> bLimitFramesInFlight_Config{true};
	std::atomic<int32> MaxFramesInFlight_Config{1};

	// Measurement, render thread only.
	uint64 NumWaits = 0;
	double WaitSeconds = 0.0;
	uint64 FramesSinceReport = 0;
	uint64 LagSumSinceReport = 0;
	int32 MaxLagSinceReport = 0;
	uint64 FramesOverBoundSinceReport = 0;
	uint64 WaitsSinceReport = 0;
	double WaitSecondsSinceReport = 0.0;
	double LastReportSeconds = 0.0;
};
