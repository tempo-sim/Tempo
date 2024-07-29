// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "PointCloud.h"
#include "Containers/UnrealString.h"
#include "Misc/Timespan.h"
#include "Containers/Map.h"



/**
 * Class for recording statistics about a RuleProcessor Generation Run. Can include timing information, integer counters and so on
 */
struct POINTCLOUD_API FPointCloudStats	
{
public:

	FPointCloudStats() = default;
	~FPointCloudStats() = default;

	/** Record timing information. Timings are accumulated to build an understanding of how long a particular class of events took *
	* @param EventName Unique Name for the timing event. 
	* @param TimeTaken The time associated with this instance of the given event
	*/
	void AddTimingToEvent(const FString& EventName, const FTimespan& TimeTaken);

	/** Add a value to a given counter. Values passed to this are accumulated for all unique values of Name
	* @param CounterName The name of the counter to add the given value to
	* @param Value The value to add to the given counter. This either creates a new counter or increments and existing one
	*/
	void AddToCounter(const FString& CounterName, int64 Value);

	/** Increment a given counter by one. If the counter does not exist a new counter will be created an initialized to one
	* @param CounterName Unique name for the counter to increment
	*/
	void IncrementCounter(const FString& CounterName);

	/** List the counter names 
	* @return The list of unique counter names
	*/
	TSet<FString> GetCounterNames() const; 

	/** List the timer names 
	* @return The list of unique timer names
	*/
	TSet<FString> GetTimerNames() const; 

	/** Get the value of a given counter 
	* @return The Value of the counter, will be 0 if the counter have not been initialized
	* @param CounterName The name of the counter to query
	*/
	int64 GetCounterValue(const FString& CounterName) const;

	/** Get the value of a given timer
	* @return The Value of the timer 
	* @param TimerName The name of the timer to query
	*/
	FTimespan GetTimerValue(const FString& TimerName) const; 

	/** Generate a human readable version of the data contained in this class 
	* @return A string containing a humand readable version of this data
	*/
	FString ToString() const; 

private:

	TMap<FString, int64> Counters;
	TMap<FString, FTimespan> Timers;
	mutable FCriticalSection Lock;
};

using FPointCloudStatsPtr=TSharedPtr<FPointCloudStats>;
