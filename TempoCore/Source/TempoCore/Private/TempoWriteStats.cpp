// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWriteStats.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"

namespace
{
	TAutoConsoleVariable<int32> CVarWriteStats(
		TEXT("Tempo.WriteStats"), 0,
		TEXT("1 records how long the server waits on its writes and what they carry (see Tempo.WriteStats.Dump). ")
		TEXT("Turning it on starts a new window."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Var)
		{
			if (Var->GetInt() != 0)
			{
				FTempoWriteStats::Reset();
			}
		}));

	// Enough for an hour of a 10-stream sim at 30 frames per second; past it, samples stop being kept
	// for percentiles, but the totals still count.
	constexpr int32 MaxSamples = 1 << 20;

	struct FState
	{
		FCriticalSection Mutex;

		double WindowStartSeconds = 0.0;
		uint64 WindowStartFrame = 0;

		int64 Writes = 0;
		int64 Bytes = 0;
		int64 QueuedResponses = 0;
		int64 Stalls = 0;
		int64 SharedMemoryMessages = 0;
		int64 SharedMemoryBytes = 0;
		int64 SharedMemoryFallbacks = 0;
		double WriteCallSeconds = 0.0;
		double WaitSeconds = 0.0;

		TArray<float> WriteLatencyMs;
		TArray<float> WriteCallMs;
		TArray<float> FrameMs;
		TArray<float> FrameWaitMs;
		TArray<double> FrameMB;

		double LastFrameStartSeconds = -1.0;
		double CurrentFrameWaitSeconds = 0.0;
		int64 CurrentFrameBytes = 0;
	};

	FState& GetState()
	{
		static FState State;
		return State;
	}

	template <typename T>
	void AddSample(TArray<T>& Samples, T Value)
	{
		if (Samples.Num() < MaxSamples)
		{
			Samples.Add(Value);
		}
	}

	// Nearest-rank percentiles of a sample set, in its own units.
	template <typename T>
	FString Distribution(TArray<T> Samples)
	{
		if (Samples.IsEmpty())
		{
			return TEXT("null");
		}
		Samples.Sort();
		double Sum = 0.0;
		for (const T Sample : Samples)
		{
			Sum += Sample;
		}
		auto Percentile = [&Samples](double P)
		{
			const int32 Index = FMath::Clamp(FMath::CeilToInt(P * Samples.Num()) - 1, 0, Samples.Num() - 1);
			return static_cast<double>(Samples[Index]);
		};
		return FString::Printf(TEXT("{\"n\":%d,\"mean\":%.3f,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f}"),
			Samples.Num(), Sum / Samples.Num(), Percentile(0.5), Percentile(0.95), Percentile(0.99), static_cast<double>(Samples.Last()));
	}

	void OnBeginFrame()
	{
		if (!FTempoWriteStats::IsEnabled())
		{
			return;
		}
		FState& State = GetState();
		const double Now = FPlatformTime::Seconds();
		FScopeLock Lock(&State.Mutex);
		if (State.LastFrameStartSeconds >= 0.0)
		{
			AddSample(State.FrameMs, static_cast<float>((Now - State.LastFrameStartSeconds) * 1000.0));
			AddSample(State.FrameWaitMs, static_cast<float>(State.CurrentFrameWaitSeconds * 1000.0));
			AddSample(State.FrameMB, State.CurrentFrameBytes / 1.e6);
		}
		State.LastFrameStartSeconds = Now;
		State.CurrentFrameWaitSeconds = 0.0;
		State.CurrentFrameBytes = 0;
	}

	FAutoConsoleCommandWithArgsAndOutputDevice DumpCommand(
		TEXT("Tempo.WriteStats.Dump"),
		TEXT("Prints the write stats of the window since the last reset as one line of JSON. 'reset' also starts a new window."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, FOutputDevice& Ar)
		{
			Ar.Log(FTempoWriteStats::ToJson());
			if (Args.Contains(TEXT("reset")))
			{
				FTempoWriteStats::Reset();
			}
		}));

	FAutoConsoleCommand ResetCommand(
		TEXT("Tempo.WriteStats.Reset"),
		TEXT("Starts a new write stats window."),
		FConsoleCommandDelegate::CreateLambda([]() { FTempoWriteStats::Reset(); }));
}

bool FTempoWriteStats::IsEnabled()
{
	return CVarWriteStats.GetValueOnAnyThread() != 0;
}

void FTempoWriteStats::RecordWrite(int64 Bytes, double WriteCallSeconds)
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	++State.Writes;
	State.Bytes += Bytes;
	State.CurrentFrameBytes += Bytes;
	State.WriteCallSeconds += WriteCallSeconds;
	AddSample(State.WriteCallMs, static_cast<float>(WriteCallSeconds * 1000.0));
}

void FTempoWriteStats::RecordWriteCompleted(double LatencySeconds)
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	AddSample(State.WriteLatencyMs, static_cast<float>(LatencySeconds * 1000.0));
}

void FTempoWriteStats::RecordQueuedResponse()
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	++State.QueuedResponses;
}

void FTempoWriteStats::RecordSharedMemory(int64 Bytes)
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	++State.SharedMemoryMessages;
	State.SharedMemoryBytes += Bytes;
	State.Bytes += Bytes;
	State.CurrentFrameBytes += Bytes;
}

void FTempoWriteStats::RecordSharedMemoryFallback()
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	++State.SharedMemoryFallbacks;
}

void FTempoWriteStats::RecordWait(double Seconds)
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	State.WaitSeconds += Seconds;
	State.CurrentFrameWaitSeconds += Seconds;
}

void FTempoWriteStats::RecordStall()
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	++State.Stalls;
}

void FTempoWriteStats::Reset()
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	State.WindowStartSeconds = FPlatformTime::Seconds();
	State.WindowStartFrame = GFrameCounter;
	State.Writes = 0;
	State.Bytes = 0;
	State.QueuedResponses = 0;
	State.Stalls = 0;
	State.SharedMemoryMessages = 0;
	State.SharedMemoryBytes = 0;
	State.SharedMemoryFallbacks = 0;
	State.WriteCallSeconds = 0.0;
	State.WaitSeconds = 0.0;
	State.WriteLatencyMs.Reset();
	State.WriteCallMs.Reset();
	State.FrameMs.Reset();
	State.FrameWaitMs.Reset();
	State.FrameMB.Reset();
	State.LastFrameStartSeconds = -1.0;
	State.CurrentFrameWaitSeconds = 0.0;
	State.CurrentFrameBytes = 0;
}

FString FTempoWriteStats::ToJson()
{
	FState& State = GetState();
	FScopeLock Lock(&State.Mutex);
	const double WindowSeconds = FMath::Max(1.e-9, FPlatformTime::Seconds() - State.WindowStartSeconds);
	const uint64 Frames = GFrameCounter - State.WindowStartFrame;
	return FString::Printf(
		TEXT("{\"enabled\":%s,\"window_s\":%.3f,\"frames\":%llu,\"frames_per_s\":%.3f,")
		TEXT("\"writes\":%lld,\"mb\":%.3f,\"mb_per_s\":%.3f,\"queued_responses\":%lld,\"stalls\":%lld,")
		TEXT("\"shm_messages\":%lld,\"shm_mb\":%.3f,\"shm_fallbacks\":%lld,")
		TEXT("\"wait_s\":%.4f,\"wait_fraction\":%.4f,\"write_call_s\":%.4f,")
		TEXT("\"frame_ms\":%s,\"frame_wait_ms\":%s,\"frame_mb\":%s,\"write_latency_ms\":%s,\"write_call_ms\":%s}"),
		IsEnabled() ? TEXT("true") : TEXT("false"), WindowSeconds, Frames, Frames / WindowSeconds,
		State.Writes, State.Bytes / 1.e6, State.Bytes / 1.e6 / WindowSeconds, State.QueuedResponses, State.Stalls,
		State.SharedMemoryMessages, State.SharedMemoryBytes / 1.e6, State.SharedMemoryFallbacks,
		State.WaitSeconds, State.WaitSeconds / WindowSeconds, State.WriteCallSeconds,
		*Distribution(State.FrameMs), *Distribution(State.FrameWaitMs), *Distribution(State.FrameMB),
		*Distribution(State.WriteLatencyMs), *Distribution(State.WriteCallMs));
}

void FTempoWriteStats::Initialize()
{
	FCoreDelegates::OnBeginFrame.AddStatic(&OnBeginFrame);
}
