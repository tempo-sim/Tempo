// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudUtils.h"
#include "PointCloudImpl.h"

namespace PointCloud
{
	/**
	* UtilityTimer class implementation 
	*/
	UtilityTimer::UtilityTimer()
	{
#if defined RULEPROCESSOR_INCLUDE_TIMING
		tick();
#endif 
	}

	int64 UtilityTimer::unixTimeNow()
	{
#if defined RULEPROCESSOR_INCLUDE_TIMING
		FDateTime timeUtc = FDateTime::UtcNow();
		return timeUtc.ToUnixTimestamp() * 1000 + timeUtc.GetMillisecond();
#else 
		return 0;
#endif 
	}

	void UtilityTimer::tick()
	{
#if defined RULEPROCESSOR_INCLUDE_TIMING
		TickTime = unixTimeNow();
#endif 
	}

	int32 UtilityTimer::tock()
	{
#if defined RULEPROCESSOR_INCLUDE_TIMING
		TockTime = unixTimeNow();
		return TockTime - TickTime;
#else
		return 0;
#endif 
	}

	float UtilityTimer::ToSeconds()
	{
#if defined RULEPROCESSOR_INCLUDE_TIMING
		float TimeInSeconds = tock() / 1000.0f;
		return TimeInSeconds;
#else 
		return 0.0f;
#endif
	}

	void UtilityTimer::Report(const FString& Name)
	{
#if defined RULEPROCESSOR_TIMERS_REPORT
		float TimeInSeconds = tock() / 1000.0f;
		if (TimeInSeconds > 0.15)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("%s : %.2f\n"), *Name, TimeInSeconds);
		}
		else
		{
			UE_LOG(PointCloudLog, Log, TEXT("%s : %.2f\n"), *Name, TimeInSeconds);
		}
		tick();
#endif 
	}

	/**
	* QueryLogger struct implementation
	*/
	QueryLogger::QueryLogger(const UPointCloudImpl* InPointCloud, const FString& InQuery, const FString& InLabel, const FString& InFile, const uint32 InLine)
	{
#if defined(RULEPROCESSOR_ENABLE_LOGGING)
		check(InPointCloud);
		PointCloud = InPointCloud;
		LogEntry = PointCloud->LogSql(InFile, InLine, InQuery);

#if defined(RULEPROCESSOR_TIMERS_REPORT)
		Query = InQuery;
		Label = InLabel;
#endif

#endif // RULEPROCESSOR_ENABLE_LOGGING
	}

	QueryLogger::~QueryLogger()
	{
#if defined(RULEPROCESSOR_ENABLE_LOGGING)

#if defined(RULEPROCESSOR_INCLUDE_TIMING)
		PointCloud->SetTiming(LogEntry, Timer.ToSeconds());
#endif

#if defined(RULEPROCESSOR_INCLUDE_TIMING) && defined(RULEPROCESSOR_TIMERS_REPORT)
		Timer.Report(Label.IsEmpty() ? Query.Left(80) : Label);
#endif

#endif // RULEPROCESSOR_ENABLE_LOGGING
	}
}