// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"

#include "MassTrafficBuilderTypes.generated.h"


UENUM(BlueprintType)
enum class EMassTrafficUser : uint8
{
	None = 0x0,
	
	Vehicle = 0x1,
	Pedestrian = 0x2,
	
	Unknown = 0xff
};


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficExplicitLaneProfileRefMapKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficUser User = EMassTrafficUser::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int NumberOfLanes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsUnidirectional = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bReverseLaneProfile = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bHasCenterDivider = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LaneWidthCM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CenterDividerWidthCM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCanSupportLongVehicles = false;
};


static bool operator==(const FMassTrafficExplicitLaneProfileRefMapKey& LHS, const FMassTrafficExplicitLaneProfileRefMapKey& RHS)
{
	return LHS.User == RHS.User &&
		LHS.NumberOfLanes == RHS.NumberOfLanes &&
		LHS.bIsUnidirectional == RHS.bIsUnidirectional &&
		LHS.bReverseLaneProfile == RHS.bReverseLaneProfile &&
		LHS.bHasCenterDivider == RHS.bHasCenterDivider &&
		FMath::IsNearlyEqual(LHS.LaneWidthCM, RHS.LaneWidthCM) &&
		FMath::IsNearlyEqual(LHS.CenterDividerWidthCM, RHS.CenterDividerWidthCM) &&
		LHS.bCanSupportLongVehicles == RHS.bCanSupportLongVehicles;
}


static uint32 GetTypeHash(const FMassTrafficExplicitLaneProfileRefMapKey& MassTrafficExplicitLaneProfileRefMapKey)
{
	uint32 Hash = 0x0; 
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficExplicitLaneProfileRefMapKey.User));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficExplicitLaneProfileRefMapKey.NumberOfLanes));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficExplicitLaneProfileRefMapKey.bIsUnidirectional));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficExplicitLaneProfileRefMapKey.bReverseLaneProfile));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficExplicitLaneProfileRefMapKey.bHasCenterDivider));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficExplicitLaneProfileRefMapKey.LaneWidthCM));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficExplicitLaneProfileRefMapKey.CenterDividerWidthCM));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficExplicitLaneProfileRefMapKey.bCanSupportLongVehicles));
	return Hash;
}


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficDebugPoint
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Point = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor Color = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Size = 0.0f;
};


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficDebugLineSegment
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Point1 = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Point2 = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor Color = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Thickness = 0.0f;
};


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficPoint
{
	GENERATED_BODY()

	FMassTrafficPoint()
	{
	}

	FMassTrafficPoint(FVector InPoint, FVector InForwardVector, FVector InUpVector, FVector InOptionalTangentVector, FVector InTrafficForwardVector, bool bInLanesMergeToOneDestination, bool bInLanesConnectWithOneLanePerDestination, bool bInLanesConnectWithNoLeftTurn, bool bInLanesConnectWithNoRightTurn) :
		Position(InPoint),
		ForwardVector(InForwardVector),
		UpVector(InUpVector),
		OptionalTangentVector(InOptionalTangentVector),
		TrafficForwardVector(InTrafficForwardVector),
		bLanesMergeToOneDestination(bInLanesMergeToOneDestination),
		bLanesConnectWithOneLanePerDestination(bInLanesConnectWithOneLanePerDestination),
		bLanesConnectWithNoLeftTurn(bInLanesConnectWithNoLeftTurn),
		bLanesConnectWithNoRightTurn(bInLanesConnectWithNoRightTurn)
	{
	}

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector ForwardVector = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector UpVector = FVector::ZeroVector;

	/**
	 * This vector is invalid (and will be ignored and/or automatically computed) when length zero. This might end up
	 * being set to non-zero by internal functionality that performs spline looping and chopping. It that case, this
	 * vector will/should be left *un-normalized* - and represents vector from previous point to next point.
	 * INTERNAL USE ONLY
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector OptionalTangentVector = FVector::ZeroVector; 

	/** Direction of traffic flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector TrafficForwardVector = FVector::ZeroVector;

	/** Lanes originating from this point (if any) should all merge to one destination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bLanesMergeToOneDestination = false;

	/** Lanes originating from this point (if any) should only connect to one destination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bLanesConnectWithOneLanePerDestination = false;

	/** Lanes originating from this point (if any) should no make left turns to arrive at a destination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bLanesConnectWithNoLeftTurn = false;

	/** Lanes originating from this point (if any) should no make right turns to arrive at a destination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bLanesConnectWithNoRightTurn = false;
};


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficRoadSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString RoadSegmentID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficUser User = EMassTrafficUser::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor DebugColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FMassTrafficPoint StartPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FMassTrafficPoint EndPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int NumberOfLanes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bHasCenterDivider = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LaneWidthCM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CenterDividerWidthCM = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCanSupportLongVehicles = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsCrosswalk = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsFreeway = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsMainPartOfFreeway = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 UserDensity = INDEX_NONE;


	FVector Midpoint() const
	{
		return 0.5f * (StartPoint.Position + EndPoint.Position);
	}
};


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficRoadSegmentMapKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString RoadSegmentID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficUser User = EMassTrafficUser::None;
};

static bool operator==(const FMassTrafficRoadSegmentMapKey& LHS, const FMassTrafficRoadSegmentMapKey& RHS)
{
	return LHS.RoadSegmentID == RHS.RoadSegmentID &&
		LHS.User == RHS.User;
}

static uint32 GetTypeHash(const FMassTrafficRoadSegmentMapKey& MassTrafficRoadSegmentMapKey)
{
	uint32 Hash = 0x0;
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficRoadSegmentMapKey.RoadSegmentID));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficRoadSegmentMapKey.User));
	return Hash;
}


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficRoadSpline
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString RoadSplineID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficUser User = EMassTrafficUser::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor DebugColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FMassTrafficPoint> Points;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int NumberOfLanes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bHasCenterDivider = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LaneWidthCM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CenterDividerWidthCM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsUnidirectional = false;

	// DEPRECATED
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsClosed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCanSupportLongVehicles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsFreeway = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsMainPartOfFreeway = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsFreewayOnramp = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsFreewayOfframp = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 UserDensity = INDEX_NONE;
};


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficRoadSplineMapKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString RoadSplineID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficUser User = EMassTrafficUser::None;
};


static bool operator==(const FMassTrafficRoadSplineMapKey& LHS, const FMassTrafficRoadSplineMapKey& RHS)
{
	return LHS.RoadSplineID == RHS.RoadSplineID &&
		LHS.User == RHS.User;
}

static uint32 GetTypeHash(const FMassTrafficRoadSplineMapKey& MassTrafficRoadSplineMapKey)
{
	uint32 Hash = 0x0;
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficRoadSplineMapKey.RoadSplineID));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficRoadSplineMapKey.User));
	return Hash;
}


UENUM(BlueprintType)
enum class EMassTrafficSpecialConnectionType : uint8
{
	None = 0x0, // No special connection needed.
	
	CityIntersectionLinkIsConnectionIsBlocked = 0x1, 

	CityIntersectionLinkConnectsRoadSegmentNeedingToBeBuilt = 0x2, 
	
	CityIntersectionLinkConnectsToIncomingFreewayRamp = 0x3, 
	CityIntersectionLinkConnectsToOutgoingFreewayRamp = 0x4, 

	FreewayIntersectionLinkConnectsToIncomingFreewayRamp = 0x5,
	FreewayIntersectionLinkConnectsToOutgoingFreewayRamp = 0x6,

	IntersectionLinkConnectsAsStraightLaneAdapter = 0x7, 
	
	Unknown = 0xff

	// IMPORTANT - Also add to AMassTrafficBuilderBaseActor's StringToSpecialConnectionType() and SpecialConnectionTypeToString()
};



USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficIntersectionLink
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString IntersectionID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int IntersectionSequenceNumber = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficUser User = EMassTrafficUser::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FMassTrafficPoint Point;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ConnectedIntersectionID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int ConnectedIntersectionSequenceNumber = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int NumberOfLanes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bHasCenterDivider = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LaneWidthCM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CenterDividerWidthCM = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsUnidirectional = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bHasTrafficLight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficSpecialConnectionType SpecialConnectionType = EMassTrafficSpecialConnectionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector TrafficLightPosition = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 UserDensity = INDEX_NONE;
};



USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficIntersection
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString IntersectionID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ParentIntersectionID = ""; // i.e. for pedestrian intersections controled by vehicle intersections. 

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficUser User = EMassTrafficUser::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FMassTrafficIntersectionLink> IntersectionLinks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor DebugColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsCenterPointValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector CenterPoint = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsCrosswalk = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCanSupportLongVehicles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsFreeway = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsMainPartOfFreeway = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsFreewayOnramp = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsFreewayOfframp = false;
};


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficIntersectionMapKey
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString IntersectionID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMassTrafficUser User = EMassTrafficUser::None;
};

static bool operator==(const FMassTrafficIntersectionMapKey& LHS, const FMassTrafficIntersectionMapKey& RHS)
{
	return LHS.IntersectionID == RHS.IntersectionID &&
		LHS.User == RHS.User;
}

static uint32 GetTypeHash(const FMassTrafficIntersectionMapKey& MassTrafficIntersectionMapKey)
{
	uint32 Hash = 0x0;
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficIntersectionMapKey.IntersectionID));
	Hash = HashCombine(Hash, GetTypeHash(MassTrafficIntersectionMapKey.User));
	return Hash;
}


USTRUCT(BlueprintType)
struct MASSTRAFFICEDITOR_API FMassTrafficPointHints
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Point = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsRoadPoint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsRoadSegmentPoint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsRoadSegmentStartPoint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsRoadSegmentEndPoint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsRoadSplinePoint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsIntersectionLinkPoint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsIntersectionCenterPoint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<FString> RoadSegmentIDs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<FString> RoadSplineIDs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<FString> IntersectionIDs;
};


UENUM(BlueprintType)
enum class EMassTrafficBuildType : uint8
{
	Components = 0x0,
	Actors = 0x1
};
