// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

// Opt-in measurements of what the server's writes cost the sim: how long the game thread waits on
// them, how long each one takes to complete, and how many bytes they carry. Off unless
// Tempo.WriteStats is 1, when every recording call is a single relaxed load and a branch.
//
// Reading them: `Tempo.WriteStats.Dump` prints the window since the last reset as one line of JSON
// (`Tempo.WriteStats.Dump reset` also starts a new window), and `Tempo.WriteStats.Reset` starts a
// new one. Both reach a client through EngineService's ExecConsoleCommand.
//
// The wait is the game thread's time blocked on unflushed writes, at the two places it blocks: a
// FixedStep server tick draining them before time moves on, and FTempoServer::FinishWrites. It is
// accumulated per game frame, so each frame's share of its wall time can be reported. Paused frames
// (a FixedStep client stepping) count as frames too, so measure with the world playing.
class TEMPOCORE_API FTempoWriteStats
{
public:
	static bool IsEnabled();

	// A streaming response handed to gRPC: its serialized size, and how long the Write call took (it
	// serializes the message into gRPC's buffers before it returns).
	static void RecordWrite(int64 Bytes, double WriteCallSeconds);

	// A streaming write's completion, the time since it was issued. It completes once the client's
	// flow-control window has room for it.
	static void RecordWriteCompleted(double LatencySeconds);

	// A response that arrived while its stream's previous write was still in flight, and so was copied
	// into the stream's queue.
	static void RecordQueuedResponse();

	// Time the game thread spent blocked on unflushed writes.
	static void RecordWait(double Seconds);

	// A request whose write outlasted Tempo.WriteStallTimeoutMs.
	static void RecordStall();

	static void Reset();
	static FString ToJson();

	// Hooks the frame boundary the per-frame figures are cut at. Called once, by the server.
	static void Initialize();
};
