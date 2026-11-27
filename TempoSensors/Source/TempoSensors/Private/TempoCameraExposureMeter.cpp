// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCameraExposureMeter.h"

#include "Misc/ScopeLock.h"
#include "RenderCommandFence.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "TextureResource.h"

FTempoCameraExposureMeter::~FTempoCameraExposureMeter()
{
	// Deleting a readback still in flight is safe: it only drops its reference to the staging buffer,
	// whose release the RHI defers until the GPU is done with it.
	for (FPending& Entry : Pending)
	{
		delete Entry.Readback;
	}
	for (FRHIGPUBufferReadback* Readback : FreeReadbacks)
	{
		delete Readback;
	}
}

void FTempoCameraExposureMeter::Enqueue(FTextureRenderTargetResource* Target, int64 CaptureId, float AppliedBias, double CaptureTime, const FSettings& Settings)
{
	if (!Target)
	{
		return;
	}

	FMeasurement Measurement;
	Measurement.CaptureId = CaptureId;
	Measurement.AppliedBias = AppliedBias;
	Measurement.CaptureTime = CaptureTime;

	ENQUEUE_RENDER_COMMAND(TempoCameraExposureMeter)(
		[Self = AsShared(), Target, Measurement, Settings](FRHICommandListImmediate& RHICmdList)
		{
			Self->ReadBack_RenderThread(RHICmdList, Measurement.CaptureId - 1, Measurement.CaptureId);

			FRHITexture* TargetTexture = Target->GetRenderTargetTexture();
			if (!TargetTexture || Self->Pending.Num() >= MaxPending)
			{
				return;
			}

			FRHIGPUBufferReadback* Readback;
			if (Self->FreeReadbacks.Num() > 0)
			{
				Readback = Self->FreeReadbacks.Pop(EAllowShrinking::No);
			}
			else
			{
				Readback = new FRHIGPUBufferReadback(TEXT("TempoCameraExposureHistogram"));
			}

			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TempoCameraExposureMeter"));
			FRDGTextureRef Input = RegisterExternalTexture(GraphBuilder, TargetTexture, TEXT("TempoCameraExposureMeter.Input"));
			FRDGBufferRef Histogram = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TempoExposureHistogramSize), TEXT("TempoCameraExposureMeter.Histogram"));
			AddTempoExposureHistogramPass(GraphBuilder, GMaxRHIFeatureLevel, Input, Settings.Histogram, Histogram);
			AddEnqueueCopyPass(GraphBuilder, Readback, Histogram, TempoExposureHistogramSize * sizeof(uint32));
			GraphBuilder.Execute();

			Self->Pending.Add({ Readback, Measurement, Settings });
		});
}

void FTempoCameraExposureMeter::ReadBack_RenderThread(FRHICommandListImmediate& RHICmdList, int64 ThroughCaptureId, int64 NewestCaptureId)
{
	check(IsInRenderingThread());
	int32 NumDone = 0;
	for (; NumDone < Pending.Num(); ++NumDone)
	{
		FPending& Entry = Pending[NumDone];
		if (Entry.Measurement.CaptureId > ThroughCaptureId)
		{
			break;
		}
		if (!Entry.Readback->IsReady())
		{
			// Usually already done on the GPU (the capture rendered a capture period ago); polling
			// alone may not see it until the RHI next processes completed work.
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraExposureMeterWait);
			Entry.Readback->Wait(RHICmdList, FRHIGPUMask::All());
		}
		const uint32* Histogram = static_cast<const uint32*>(Entry.Readback->Lock(TempoExposureHistogramSize * sizeof(uint32)));
		float Luminance = 0.0f;
		const bool bValid = Histogram && AverageLuminance(Histogram, Entry.Settings, Luminance);
		Entry.Readback->Unlock();
		if (bValid)
		{
			Entry.Measurement.MeasuredLuminance = Luminance;
			Entry.Measurement.ReadAtCaptureId = NewestCaptureId;
			FScopeLock Lock(&CompletedLock);
			Completed.Add(Entry.Measurement);
		}
		FreeReadbacks.Add(Entry.Readback);
	}
	Pending.RemoveAt(0, NumDone, EAllowShrinking::No);
	if (ThroughCaptureId > ReadBackThroughCaptureId.load())
	{
		ReadBackThroughCaptureId.store(ThroughCaptureId);
	}
}

void FTempoCameraExposureMeter::ConsumeThrough(int64 CaptureId, TArray<FMeasurement>& Out)
{
	check(IsInGameThread());
	if (ReadBackThroughCaptureId.load() < CaptureId)
	{
		// The render thread hasn't metered the capture after CaptureId yet; read back through it now.
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraExposureMeterConsumeWait);
		ENQUEUE_RENDER_COMMAND(TempoCameraExposureMeterReadBack)(
			[Self = AsShared(), CaptureId](FRHICommandListImmediate& RHICmdList)
			{
				Self->ReadBack_RenderThread(RHICmdList, CaptureId, CaptureId);
			});
		FRenderCommandFence Fence;
		Fence.BeginFence();
		Fence.Wait();
	}

	FScopeLock Lock(&CompletedLock);
	int32 Num = 0;
	while (Num < Completed.Num() && Completed[Num].CaptureId <= CaptureId)
	{
		++Num;
	}
	Out.Reset();
	Out.Append(Completed.GetData(), Num);
	Completed.RemoveAt(0, Num, EAllowShrinking::No);
}

bool FTempoCameraExposureMeter::AverageLuminance(const uint32* Histogram, const FSettings& Settings, float& OutLuminance)
{
	const float LogMax = Settings.Histogram.HistogramLogMax;
	const float LogMin = FMath::Min(Settings.Histogram.HistogramLogMin, LogMax - 1.0f);

	double Sum = 0.0;
	for (int32 Index = 0; Index < TempoExposureHistogramSize; ++Index)
	{
		Sum += Histogram[Index];
	}
	if (Sum <= 0.0)
	{
		return false;
	}

	// The engine's special case: an empty percentile band means a fixed average of one.
	if (FMath::Abs(Settings.LowFraction - Settings.HighFraction) < 0.0001f)
	{
		OutLuminance = 1.0f;
		return true;
	}

	// ComputeAverageLuminanceWithoutOutlier: drop the lowest LowFraction and everything above
	// HighFraction of the weight, and average the log luminance of what remains.
	double MinFractionSum = Sum * Settings.LowFraction;
	double MaxFractionSum = Sum * Settings.HighFraction;
	double SumLog = 0.0;
	double SumWeight = 0.0;
	for (int32 Index = 0; Index < TempoExposureHistogramSize; ++Index)
	{
		double Value = Histogram[Index];

		const double Sub = FMath::Min(Value, MinFractionSum);
		Value -= Sub;
		MinFractionSum -= Sub;
		MaxFractionSum -= Sub;

		Value = FMath::Min(Value, MaxFractionSum);
		MaxFractionSum -= Value;

		const double Position = static_cast<double>(Index) / (TempoExposureHistogramSize - 1);
		const double LogLuminance = LogMin + Position * (LogMax - LogMin);
		SumLog += LogLuminance * Value;
		SumWeight += Value;
	}
	if (SumWeight <= 0.0)
	{
		return false;
	}

	OutLuminance = static_cast<float>(FMath::Exp2(SumLog / SumWeight));
	return true;
}
