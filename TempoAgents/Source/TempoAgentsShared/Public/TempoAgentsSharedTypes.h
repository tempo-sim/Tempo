﻿// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

UENUM(Blueprintable, BlueprintType)
enum class ETempoCoordinateSpace : uint8
{
	Local = 0x0,
	World = 0x1,
};

UENUM(BlueprintType)
enum class ETempoRoadOffsetOrigin : uint8
{
	CenterSpline = 0,
	LeftRoadEdge = 1,
	RightRoadEdge = 2,
};

UENUM(BlueprintType)
enum class ETempoRoadLateralDirection : uint8
{
	Left = 0,
	Right = 1,
};


