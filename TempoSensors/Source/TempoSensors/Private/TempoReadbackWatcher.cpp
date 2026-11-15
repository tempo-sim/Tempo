// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoReadbackWatcher.h"

#include "TempoSceneCaptureComponent2D.h"

#include "DynamicRHI.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
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
	GReadbackWatcher->Incoming.Enqueue(TWeakPtr<FTextureRead>(Read));
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

	TArray<TWeakPtr<FTextureRead>> Pending;
	while (!bStop)
	{
		TWeakPtr<FTextureRead> Watched;
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
		const TSharedPtr<FTextureRead> Read = Pending[0].Pin();
		Pending.RemoveAt(0);
		// Dropped by its queue, or read by one of the render thread's polls already.
		if (!Read || Read->State != FTextureRead::State::EAwaitingRender)
		{
			continue;
		}
		if (!Read->RenderFence->Poll())
		{
			// The Vulkan and D3D12 waits do not touch the command list; the accessor that asserts
			// the render thread is not used here.
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsReadbackWatcherWait);
			Read->RenderFence->Wait(FRHICommandListExecutor::GetImmediateCommandList(), FRHIGPUMask::All());
		}
		Read->Read();
	}

	GReadbackWatcherThreadId = 0;
	return 0;
}
