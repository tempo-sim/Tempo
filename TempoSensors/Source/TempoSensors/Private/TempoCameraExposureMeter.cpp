// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCameraExposureMeter.h"

#include "Misc/ScopeLock.h"
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
			Self->Poll_RenderThread(Measurement.CaptureId);

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

void FTempoCameraExposureMeter::Poll_RenderThread(int64 NewestCaptureId)
{
	check(IsInRenderingThread());

	// Readbacks complete in submission order; stop at the first that hasn't.
	int32 NumDone = 0;
	for (; NumDone < Pending.Num(); ++NumDone)
	{
		FPending& Entry = Pending[NumDone];
		if (!Entry.Readback->IsReady())
		{
			break;
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
}

void FTempoCameraExposureMeter::Consume(TArray<FMeasurement>& Out)
{
	FScopeLock Lock(&CompletedLock);
	Out = MoveTemp(Completed);
	Completed.Reset();
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
