// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloudConfig.h"

class UPointCloudImpl;

namespace PointCloud
{
	class UtilityTimer
	{
	public:
		UtilityTimer();
		float ToSeconds();
		void Report(const FString& Name);

	private:
		int64 unixTimeNow();
		void tick();
		int32 tock();

#if defined RULEPROCESSOR_INCLUDE_TIMING
		int64 TickTime = 0;
		int64 TockTime = 0;
#endif 
	};

	struct QueryLogger
	{
		QueryLogger(const UPointCloudImpl* InPointCloud, const FString& InQuery, const FString& InLabel = FString(), const FString& InFile = FString(), const uint32 InLine = 0);
		~QueryLogger();

#if defined(RULEPROCESSOR_ENABLE_LOGGING)
		const UPointCloud* PointCloud;
		UPointCloud::LogEntry LogEntry;

#if defined(RULEPROCESSOR_INCLUDE_TIMING)
		UtilityTimer Timer;
#endif

#if defined(RULEPROCESSOR_INCLUDE_TIMING) && defined(RULEPROCESSOR_TIMERS_REPORT)
		FString Query;
		FString Label;
#endif
#endif // RULEPROCESSOR_ENABLE_LOGGING
	};
}
