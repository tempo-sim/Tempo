// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoReadbackWatcher.h"

#include "TempoSceneCaptureComponent2D.h"

#include "DynamicRHI.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"

namespace
{
	FTempoReadbackWatcher* GReadbackWatcher = nullptr;
	std::atomic<uint32> GReadbackWatcherThreadId = 0;
}

bool FTempoReadbackWatcher::IsActive()
{
	const ERHIInterfaceType RHIType = RHIGetInterfaceType();
	return RHIType == ERHIInterfaceType::Vulkan || RHIType == ERHIInterfaceType::D3D12;
}

bool FTempoReadbackWatcher::IsInWatcherThread()
{
	const uint32 ThreadId = GReadbackWatcherThreadId.load(std::memory_order_relaxed);
	return ThreadId != 0 && ThreadId == FPlatformTLS::GetCurrentThreadId();
}

void FTempoReadbackWatcher::Watch(const TSharedRef<FTextureRead>& Read)
{
	check(IsInRenderingThread());

	if (!GReadbackWatcher)
	{
		GReadbackWatcher = new FTempoReadbackWatcher();
	}
	GReadbackWatcher->Incoming.Enqueue(FWatched{ TWeakPtr<FTextureRead>(Read), FPlatformTime::Seconds() });
	GReadbackWatcher->WakeEvent->Trigger();
}

void FTempoReadbackWatcher::Shutdown()
{
	if (GReadbackWatcher)
	{
		delete GReadbackWatcher;
		GReadbackWatcher = nullptr;
	}
}

FTempoReadbackWatcher::FTempoReadbackWatcher()
	: WakeEvent(FPlatformProcess::GetSynchEventFromPool(false))
{
	Thread = FRunnableThread::Create(this, TEXT("TempoReadbackWatcher"), 0, TPri_AboveNormal);
}

FTempoReadbackWatcher::~FTempoReadbackWatcher()
{
	if (Thread)
	{
		// Stops the thread, then joins it. A wait in progress ends with the GPU work it waits for.
		Thread->Kill(true);
		delete Thread;
	}
	FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
}

void FTempoReadbackWatcher::Stop()
{
	bStop = true;
	WakeEvent->Trigger();
}

uint32 FTempoReadbackWatcher::Run()
{
	GReadbackWatcherThreadId = FPlatformTLS::GetCurrentThreadId();

	// Without the RHI's interrupt thread, FRHIGPUFence::Wait polls the submission queues in a loop
	// for as long as the GPU takes. Per sensor, the shortest recent time from Watch() to the fence's
	// signal: the thread sleeps until SpinMargin before that, then waits. The estimate drifts up
	// slowly, so a wait that ends early only costs a little spinning until it has caught up, and
	// falls at once to any shorter time seen, so the sleep never runs past a signal for long.
	constexpr double SpinMargin = 0.0005;
	constexpr double MaxSleepSlice = 0.002;
	constexpr double EstimateRise = 0.02;
	TMap<FString, double> FastestSignal;

	TArray<FWatched> Pending;
	while (!bStop)
	{
		FWatched Watched;
		while (Incoming.Dequeue(Watched))
		{
			Pending.Add(MoveTemp(Watched));
		}
		if (Pending.IsEmpty())
		{
			WakeEvent->Wait();
			continue;
		}

		// Oldest first: the copies are submitted in order, so their fences signal in order.
		const FWatched Next = Pending[0];
		const TSharedPtr<FTextureRead> Read = Next.Read.Pin();
		Pending.RemoveAt(0);
		// Dropped by its queue, or read by one of the render thread's polls already.
		if (!Read || Read->State != FTextureRead::State::EAwaitingRender)
		{
			continue;
		}
		const FString SensorKey = Read->OwnerName + TEXT("/") + Read->SensorName;
		if (!Read->RenderFence->Poll())
		{
			if (const double* Fastest = FastestSignal.Find(SensorKey))
			{
				const double WakeAt = Next.WatchedAt + *Fastest - SpinMargin;
				for (double Now = FPlatformTime::Seconds(); Now < WakeAt && !bStop; Now = FPlatformTime::Seconds())
				{
					FPlatformProcess::SleepNoStats(static_cast<float>(FMath::Min(WakeAt - Now, MaxSleepSlice)));
					if (Read->RenderFence->Poll())
					{
						break;
					}
				}
			}
			// The Vulkan and D3D12 waits do not touch the command list; the accessor that asserts
			// the render thread is not used here.
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsReadbackWatcherWait);
			Read->RenderFence->Wait(FRHICommandListExecutor::GetImmediateCommandList(), FRHIGPUMask::All());
		}
		const double SignalledAfter = FPlatformTime::Seconds() - Next.WatchedAt;
		double& Fastest = FastestSignal.FindOrAdd(SensorKey, SignalledAfter);
		Fastest = SignalledAfter < Fastest ? SignalledAfter : Fastest + (SignalledAfter - Fastest) * EstimateRise;

		Read->Read();
	}

	GReadbackWatcherThreadId = 0;
	return 0;
}
