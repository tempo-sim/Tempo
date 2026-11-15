// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"

#include <atomic>

struct FTextureRead;
class FEvent;
class FRunnableThread;

// Reads a sensor's staging copy back the moment its render fence signals, from a thread of its own
// that waits on the fence, instead of the render thread polling the fence between the frame's
// captures and between a client's steps. A fence that signals while the render thread encodes the
// next frame's captures, or while the game thread ticks that frame, is otherwise read at the next
// poll, one capture's encode or one step later.
//
// The thread waits through FRHIGPUFence::Wait. On Vulkan and D3D12 that processes the RHI's
// completed GPU work on the calling thread, under the RHI's own lock, the way the RHI's interrupt
// thread does on machines where it runs one, and the staging texture of a signalled fence is mapped
// without the render thread. Metal maps through the render thread's command list, so there the
// watcher stays off and the render thread's polls read every fence, as before.
//
// Process-wide, like the GPU frame throttle: started by the first read it is handed, stopped by the
// module. A read is watched through a weak pointer, so one its queue drops is simply skipped.
class TEMPOSENSORS_API FTempoReadbackWatcher final : public FRunnable
{
public:
	// Whether the watcher reads the fences on this RHI.
	static bool IsActive();

	// Whether the calling thread is the watcher's: a read may run there as well as on the render thread.
	static bool IsInWatcherThread();

	// Render thread, once the read's RenderFence is written. The watcher reads it back when the fence
	// signals, unless the render thread's own poll gets there first.
	static void Watch(const TSharedRef<FTextureRead>& Read);

	// Stops the thread and joins it. From the module's shutdown, while the RHI is still up.
	static void Shutdown();

	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	FTempoReadbackWatcher();
	virtual ~FTempoReadbackWatcher() override;

	TQueue<TWeakPtr<FTextureRead>, EQueueMode::Mpsc> Incoming;
	FEvent* WakeEvent;
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> bStop = false;
};
