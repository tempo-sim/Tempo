// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera.h"

struct FTempoCameraInfo
{
	FTempoCameraInfo(const FTempoCameraIntrinsics& IntrinsicsIn, const FString& FrameIdIn, float TimestampIn)
		: Intrinsics(IntrinsicsIn), FrameId(FrameIdIn), Timestamp(TimestampIn) {}
	
	FTempoCameraIntrinsics Intrinsics;
	FString FrameId;
	float Timestamp;
};
