// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"

// Which capture may answer a camera's image request (see UTempoCamera::StreamLastSequenceIds). A
// stream is its response delegate: a requester that wants to be one stream across requests passes
// the same delegate, or a copy of it, each time.
namespace TempoCameraStreamRequests
{
	// The oldest capture that may answer a request arriving now on Stream. A stream already being
	// answered may take any capture newer than its last one. A new stream may take only a capture
	// that hasn't been taken yet: NextSequenceId is the id the next capture gets.
	inline int32 GetMinSequenceId(const TMap<FDelegateHandle, int32>& StreamLastSequenceIds, const FDelegateHandle& Stream, int32 NextSequenceId)
	{
		if (const int32* LastSequenceId = StreamLastSequenceIds.Find(Stream))
		{
			return *LastSequenceId + 1;
		}
		return NextSequenceId;
	}

	// Take from Pending the requests that a read with ReadSequenceId may answer, in order, and record
	// it as the last capture their streams were answered with. The rest stay pending, for a newer capture.
	template <typename ImageRequestType>
	TArray<ImageRequestType> TakeRequestsAnsweredBy(TArray<ImageRequestType>& Pending, int32 ReadSequenceId, TMap<FDelegateHandle, int32>& StreamLastSequenceIds)
	{
		TArray<ImageRequestType> Taken;
		for (int32 Index = 0; Index < Pending.Num();)
		{
			if (Pending[Index].MinSequenceId <= ReadSequenceId)
			{
				StreamLastSequenceIds.Add(Pending[Index].ResponseContinuation.GetHandle(), ReadSequenceId);
				Taken.Add(MoveTemp(Pending[Index]));
				Pending.RemoveAt(Index, EAllowShrinking::No);
			}
			else
			{
				++Index;
			}
		}
		return Taken;
	}
}
