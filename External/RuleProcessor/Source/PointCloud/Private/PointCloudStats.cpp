// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudStats.h"
#include "Misc/StringBuilder.h"


void FPointCloudStats::AddTimingToEvent(const FString& EventName, const FTimespan& TimeTaken)
{
	FScopeLock Locker(&Lock);

	if (Timers.Contains(EventName))
	{
		Timers[EventName] += TimeTaken;
	}
	else
	{
		Timers.Add(EventName, TimeTaken);
	}
}

void FPointCloudStats::AddToCounter(const FString& CounterName, int64 Value)
{
	FScopeLock Locker(&Lock);

	if (Counters.Contains(CounterName))
	{
		Counters[CounterName] += Value;
	}
	else
	{
		Counters.Add(CounterName, Value);
	}
}

void FPointCloudStats::IncrementCounter(const FString& CounterName)
{
	FScopeLock Locker(&Lock);
	
	if (Counters.Contains(CounterName))
	{
		Counters[CounterName]++;
	}
	else
	{
		Counters.Add(CounterName, 1);
	}
}

TSet<FString> FPointCloudStats::GetCounterNames() const
{
	FScopeLock Locker(&Lock);

	TSet<FString> Result;
	Counters.GetKeys(Result);
	return Result;
}

TSet<FString> FPointCloudStats::GetTimerNames() const
{
	FScopeLock Locker(&Lock);

	TSet<FString> Result;
	Timers.GetKeys(Result);
	return Result;
}

int64 FPointCloudStats::GetCounterValue(const FString& CounterName) const
{
	FScopeLock Locker(&Lock);

	const int64* ValuePtr = Counters.Find(CounterName);

	if (ValuePtr)
	{
		return *ValuePtr;
	}
	else
	{
		return 0;
	}
}

FTimespan FPointCloudStats::GetTimerValue(const FString& TimerName) const
{
	FScopeLock Locker(&Lock);

	if (Timers.Contains(TimerName) == false)
	{
		return 0;
	}
	else
	{
		return Timers[TimerName];
	}	
}

FString FPointCloudStats::ToString() const
{
	FScopeLock Locker(&Lock);

	TStringBuilder<1024> StringBuilder;

	if (Counters.Num())
	{
		StringBuilder.Append(TEXT("\nCounters\n"));
		StringBuilder.Append(TEXT("==================\n"));

		for (const auto& Entry : Counters)
		{
			StringBuilder.Appendf(TEXT("%s=%ld\n"), *Entry.Key, Entry.Value);
		}
	}

	if (Timers.Num())
	{
		StringBuilder.Append(TEXT("\nTimers\n"));
		StringBuilder.Append(TEXT("==================\n"));

		for (const auto& Entry : Timers)
		{
			StringBuilder.Appendf(TEXT("%s=%s\n"), *Entry.Key, *Entry.Value.ToString());
		}
	}

	return StringBuilder.ToString();
}
