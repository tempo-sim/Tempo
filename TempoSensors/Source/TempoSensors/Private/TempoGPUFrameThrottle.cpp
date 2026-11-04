// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGPUFrameThrottle.h"

#include "TempoSensors.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RenderingThread.h"

namespace
{
	// How often the lag summary is written.
	constexpr double ReportPeriodSeconds = 60.0;
}

TOptional<uint64> FTempoGPUFrameThrottle::FrameToWaitFor(uint64 Frame, int32 MaxFramesInFlight)
{
	const uint64 Bound = static_cast<uint64>(FMath::Max(MaxFramesInFlight, 1));
	if (Frame < Bound)
	{
		return TOptional<uint64>();
	}
	return Frame - Bound;
}

int32 FTempoGPUFrameThrottle::MeasureLag(uint64 Frame, int32 Window, TFunctionRef<bool(uint64)> IsFrameComplete)
{
	int32 Lag = 0;
	for (uint64 Back = 1; Back <= static_cast<uint64>(Window) && Back <= Frame; ++Back)
	{
		if (IsFrameComplete(Frame - Back))
		{
			break;
		}
		++Lag;
	}
	return Lag;
}

void FTempoGPUFrameThrottle::Configure(bool bLimitFramesInFlight, int32 MaxFramesInFlight)
{
	bLimitFramesInFlight_Config.store(bLimitFramesInFlight, std::memory_order_relaxed);
	MaxFramesInFlight_Config.store(FMath::Clamp(MaxFramesInFlight, 1, MeasureWindow), std::memory_order_relaxed);
}

bool FTempoGPUFrameThrottle::IsFrameComplete(uint64 Frame) const
{
	if (Slots.IsEmpty())
	{
		return true;
	}
	const FSlot& Slot = Slots[SlotForFrame(Frame)];
	// A slot that has been overwritten by a later frame, or never written, carries no information
	// about Frame; the frames it would have held are older than the window and are treated as done.
	if (Slot.Frame != Frame || !Slot.Fence.IsValid())
	{
		return true;
	}
	return Slot.Fence->Poll();
}

void FTempoGPUFrameThrottle::WaitForFrame(FRHICommandListImmediate& RHICmdList, uint64 Frame)
{
	FSlot& Slot = Slots[SlotForFrame(Frame)];
	if (Slot.Frame != Frame || !Slot.Fence.IsValid() || Slot.Fence->Poll())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsGPUFrameThrottleWait);
	const double Start = FPlatformTime::Seconds();

	// The fence write must have reached the RHI before it can be waited on. With an RHI thread, wait
	// for it to have translated that frame's commands and no further (the frame's own fence); without
	// one, dispatching executes the queued commands here.
	if (Slot.RHIThreadFence.IsValid())
	{
		FRHICommandListExecutor::WaitOnRHIThreadFence(Slot.RHIThreadFence);
	}
	else
	{
		RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}
	Slot.Fence->Wait(RHICmdList, FRHIGPUMask::All());

	const double Seconds = FPlatformTime::Seconds() - Start;
	++NumWaits;
	WaitSeconds += Seconds;
	++WaitsSinceReport;
	WaitSecondsSinceReport += Seconds;
}

void FTempoGPUFrameThrottle::OnEndFrameRT(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());

	if (GUsingNullRHI || IsRunningCommandlet())
	{
		return;
	}

	if (Slots.Num() != NumSlots())
	{
		Slots.SetNum(NumSlots());
	}

	const uint64 Frame = NextFrame++;
	const bool bLimit = bLimitFramesInFlight_Config.load(std::memory_order_relaxed);
	const int32 MaxFramesInFlight = MaxFramesInFlight_Config.load(std::memory_order_relaxed);

	// Measure before waiting: this is the lag the GPU actually reached, bound or no bound.
	const int32 Lag = MeasureLag(Frame, MeasureWindow, [this](uint64 EarlierFrame) { return IsFrameComplete(EarlierFrame); });
	++FramesSinceReport;
	LagSumSinceReport += Lag;
	MaxLagSinceReport = FMath::Max(MaxLagSinceReport, Lag);
	if (Lag > MaxFramesInFlight)
	{
		++FramesOverBoundSinceReport;
	}

	if (bLimit)
	{
		if (const TOptional<uint64> FrameToWait = FrameToWaitFor(Frame, MaxFramesInFlight))
		{
			WaitForFrame(RHICmdList, FrameToWait.GetValue());
		}
	}

	// This frame's fence, behind everything the frame rendered. Written last so the wait above never
	// has to translate this frame's commands to find the one it needs.
	FSlot& Slot = Slots[SlotForFrame(Frame)];
	Slot.Frame = Frame;
	Slot.Fence = RHICreateGPUFence(TEXT("TempoGPUFrameThrottle"));
	RHICmdList.WriteGPUFence(Slot.Fence);
	Slot.RHIThreadFence = RHICmdList.RHIThreadFence();

	Report(MaxFramesInFlight, bLimit);
}

void FTempoGPUFrameThrottle::Report(int32 MaxFramesInFlight, bool bLimitFramesInFlight)
{
	const double Now = FPlatformTime::Seconds();
	if (LastReportSeconds <= 0.0)
	{
		LastReportSeconds = Now;
		return;
	}
	if (Now - LastReportSeconds < ReportPeriodSeconds || FramesSinceReport == 0)
	{
		return;
	}

	const double MeanLag = static_cast<double>(LagSumSinceReport) / static_cast<double>(FramesSinceReport);
	// Worth a line at Log level only when the bound mattered: it was enforced, or it would have been.
	const bool bNotable = WaitsSinceReport > 0 || FramesOverBoundSinceReport > 0;
	const FString Summary = FString::Printf(
		TEXT("GPU frame throttle (%s, bound %d): over %llu frames the GPU was behind by up to %d frames (mean %.2f), over the bound on %llu; waited %llu times, %.1f ms total."),
		bLimitFramesInFlight ? TEXT("on") : TEXT("measuring only"), MaxFramesInFlight,
		FramesSinceReport, MaxLagSinceReport, MeanLag, FramesOverBoundSinceReport,
		WaitsSinceReport, WaitSecondsSinceReport * 1000.0);
	if (bNotable)
	{
		UE_LOG(LogTempoSensors, Log, TEXT("%s"), *Summary);
	}
	else
	{
		UE_LOG(LogTempoSensors, Verbose, TEXT("%s"), *Summary);
	}

	LastReportSeconds = Now;
	FramesSinceReport = 0;
	LagSumSinceReport = 0;
	MaxLagSinceReport = 0;
	FramesOverBoundSinceReport = 0;
	WaitsSinceReport = 0;
	WaitSecondsSinceReport = 0.0;
}

void FTempoGPUFrameThrottle::Release()
{
	check(IsInRenderingThread());
	Slots.Empty();
}
