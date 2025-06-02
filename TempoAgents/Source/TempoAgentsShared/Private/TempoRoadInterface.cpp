// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoRoadInterface.h"

#include "TempoAgentsSettings.h"
#include "TempoAgentsShared.h"
#include "ZoneGraphTypes.h"
#include "Components/SplineComponent.h"

ESplineCoordinateSpace::Type inline SplineCoordinateSpace(ETempoCoordinateSpace TempoCoordinateSpace)
{
	switch (TempoCoordinateSpace)
	{
		case ETempoCoordinateSpace::World:
			{
				return ESplineCoordinateSpace::World;
			}
		case ETempoCoordinateSpace::Local:
			{
				return ESplineCoordinateSpace::Local;
			}
	}

	ensureMsgf(false, TEXT("Unhandled Tempo Coordinate Space Enum when converting to SplineCoordinateSpace"));
	return ESplineCoordinateSpace::World;
}

void ITempoRoadInterface::TestVoidFunction_Implementation(bool& ReturnVar) const
{
	ReturnVar = true;
}

bool ITempoRoadInterface::ShouldGenerateZoneShapesForTempoRoad_Implementation() const
{
	return true;
}

float ITempoRoadInterface::GetTempoRoadWidth_Implementation() const
{
	float Width = 0.0;

	// Lanes
	for (int32 I = 0; I < GetNumTempoLanes(); ++I)
	{
		Width += GetTempoLaneWidth(I);
	}

	// Shoulders
	Width += GetTempoRoadShoulderWidth(ETempoRoadLateralDirection::Left);
	Width += GetTempoRoadShoulderWidth(ETempoRoadLateralDirection::Right);

	return Width;
}

float ITempoRoadInterface::GetTempoRoadShoulderWidth_Implementation(ETempoRoadLateralDirection LateralSide) const
{
	if (const UTempoAgentsSettings* TempoAgentsSettings = GetDefault<UTempoAgentsSettings>())
	{
		return TempoAgentsSettings->GetDefaultRoadShoulderWidth();
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find TempoAgentsSettings in GetTempoRoadShoulderWidth_Implementation"));
	return 0.0;
}

bool ITempoRoadInterface::IsTempoRoadClosedLoop_Implementation() const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->IsClosedLoop();
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in IsTempoRoadClosedLoop_Implementation. Is GetRoadSpline() implemented?"));
	return false;
}

float ITempoRoadInterface::GetTempoRoadSampleDistanceStepSize_Implementation() const
{
	if (const UTempoAgentsSettings* TempoAgentsSettings = GetDefault<UTempoAgentsSettings>())
	{
		return TempoAgentsSettings->GetDefaultRoadSampleDistanceStepSize();
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find TempoAgentsSettings in GetTempoRoadSampleDistanceStepSize_Implementation"));
	return 0.0;
}

int32 ITempoRoadInterface::GetNumTempoLanes_Implementation() const
{
	if (const UTempoAgentsSettings* TempoAgentsSettings = GetDefault<UTempoAgentsSettings>())
	{
		return TempoAgentsSettings->GetDefaultNumRoadLanes();
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find TempoAgentsSettings in GetNumTempoLanes_Implementation"));
	return 0;
}

float ITempoRoadInterface::GetTempoLaneWidth_Implementation(int32 LaneIndex) const
{
	if (const UTempoAgentsSettings* TempoAgentsSettings = GetDefault<UTempoAgentsSettings>())
	{
		return TempoAgentsSettings->GetDefaultRoadLaneWidth();
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find TempoAgentsSettings in GetTempoLaneWidth_Implementation"));
	return 0.0;
}

EZoneLaneDirection ITempoRoadInterface::GetTempoLaneDirection_Implementation(int32 LaneIndex) const
{
	const int32 NumLanes = GetNumTempoLanes();
	return LaneIndex < NumLanes / 2 ? EZoneLaneDirection::Forward : EZoneLaneDirection::Backward;
}

TArray<FName> ITempoRoadInterface::GetTempoLaneTags_Implementation(int32 LaneIndex) const
{
	if (const UTempoAgentsSettings* TempoAgentsSettings = GetDefault<UTempoAgentsSettings>())
	{
		return TempoAgentsSettings->GetDefaultRoadLaneTags();
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find TempoAgentsSettings in GetTempoLaneTags_Implementation"));
	return TArray<FName>();
}

int32 ITempoRoadInterface::GetNumTempoControlPoints_Implementation() const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetNumberOfSplinePoints();
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetNumTempoControlPoints_Implementation. Is GetRoadSpline() implemented?"));
	return 0;
}

FVector ITempoRoadInterface::GetTempoControlPointLocation_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetLocationAtSplinePoint(ControlPointIndex, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetTempoControlPointLocation_Implementation. Is GetRoadSpline() implemented?"));
	return FVector::ZeroVector;
}

FVector ITempoRoadInterface::GetTempoControlPointTangent_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetTangentAtSplinePoint(ControlPointIndex, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetTempoControlPointTangent_Implementation. Is GetRoadSpline() implemented?"));
	return FVector::ZeroVector;
}

FVector ITempoRoadInterface::GetTempoControlPointUpVector_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetUpVectorAtSplinePoint(ControlPointIndex, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetTempoControlPointUpVector_Implementation. Is GetRoadSpline() implemented?"));
	return FVector::ZeroVector;
}

FVector ITempoRoadInterface::GetTempoControlPointRightVector_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetRightVectorAtSplinePoint(ControlPointIndex, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetTempoControlPointRightVector_Implementation. Is GetRoadSpline() implemented?"));
	return FVector::ZeroVector;
}

FRotator ITempoRoadInterface::GetTempoControlPointRotation_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetRotationAtSplinePoint(ControlPointIndex, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetTempoControlPointRotation_Implementation. Is GetRoadSpline() implemented?"));
	return FRotator::ZeroRotator;
}

float ITempoRoadInterface::GetDistanceAlongTempoRoadAtControlPoint_Implementation(int32 ControlPointIndex) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetDistanceAlongSplineAtSplinePoint(ControlPointIndex);
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetDistanceAlongTempoRoadAtControlPoint_Implementation. Is GetRoadSpline() implemented?"));
	return 0.0;
}

float ITempoRoadInterface::GetDistanceAlongTempoRoadClosestToWorldLocation_Implementation(FVector QueryLocation) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetDistanceAlongSplineAtLocation(QueryLocation, ESplineCoordinateSpace::World);
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetDistanceAlongTempoRoadClosestToWorldLocation_Implementation. Is GetRoadSpline() implemented?"));
	return 0.0;
}

FVector ITempoRoadInterface::GetLocationAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetLocationAtDistanceAlongSpline(DistanceAlongRoad, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetLocationAtDistanceAlongTempoRoad_Implementation. Is GetRoadSpline() implemented?"));
	return FVector::ZeroVector;
}

FVector ITempoRoadInterface::GetTangentAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetTangentAtDistanceAlongSpline(DistanceAlongRoad, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetTangentAtDistanceAlongTempoRoad_Implementation. Is GetRoadSpline() implemented?"));
	return FVector::ZeroVector;
}

FVector ITempoRoadInterface::GetUpVectorAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetUpVectorAtSplinePoint(DistanceAlongRoad, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetUpVectorAtDistanceAlongTempoRoad_Implementation. Is GetRoadSpline() implemented?"));
	return FVector::ZeroVector;
}

FVector ITempoRoadInterface::GetRightVectorAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetRightVectorAtDistanceAlongSpline(DistanceAlongRoad, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetRightVectorAtDistanceAlongTempoRoad_Implementation. Is GetRoadSpline() implemented?"));
	return FVector::ZeroVector;
}

FRotator ITempoRoadInterface::GetRotationAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const
{
	if (const USplineComponent* RoadSpline = GetRoadSpline())
	{
		return RoadSpline->GetRotationAtDistanceAlongSpline(DistanceAlongRoad, SplineCoordinateSpace(CoordinateSpace));
	}

	UE_LOG(LogTempoAgentsShared, Error, TEXT("Unable to find RoadSpline in GetRightVectorAtDistanceAlongTempoRoad_Implementation. Is GetRoadSpline() implemented?"));
	return FRotator::ZeroRotator;
}
