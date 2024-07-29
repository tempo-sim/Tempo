// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "ZoneGraphData.h"
#include "EditorUtilityActor.h"
#include "ZoneGraphTypes.h"

#include "MassTrafficZoneGraphDataModifier.generated.h"

UENUM(BlueprintType)
enum class EMassTrafficZoneGraphModifierTraceType : uint8
{
	Line = 0,
	Sphere = 1
};


UCLASS(BlueprintType)
class AMassTrafficZoneGraphDataModifier : public AEditorUtilityActor
{
	GENERATED_BODY()

public:
	AMassTrafficZoneGraphDataModifier();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone Graph")
	AZoneGraphData* ZoneGraphData;

	UFUNCTION(BlueprintCallable, Category="Snap to Ground")
	void BuildZoneGraphData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snap")
	bool bSnapPointZ = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snap")
	bool bSnapPointUpVector = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snap")
	bool bForceUpVectorPositiveZ = true;
	
	/**
	 * Trace type:
	 * Line - Faster, but should only be used when there are no cracks in the geometry.
	 * Sphere - Slower, but should be used if there are cracks in the geometry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace")
	EMassTrafficZoneGraphModifierTraceType TraceType = EMassTrafficZoneGraphModifierTraceType::Sphere; 

	/** If using box trace, this is half the width of the box. */ 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace")
	float TraceSphereRadius = 12.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace")
	float TraceStartZOffset = 130.0f; 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace")
	float TraceEndZOffset = -100.0f; 

	/** Any additional Z offset to apply to the final point, once it's found. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace")
	float TraceFinalZOffset = 0.0f; 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace Debug")
	bool bTraceDebugDrawTrace = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace Debug")
	bool bTraceDebugDrawHits = false; 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace Debug")
	bool bTraceDebugDrawMisses = false; 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snap to Ground")
	FZoneGraphTagMask GroundSnapIncludeTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snap to Ground")
	FZoneGraphTagMask GroundSnapExcludeTags;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snap to Ground")
	bool GroundSnapTraceComplex = true; 

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snap to Ground")
	TEnumAsByte<ECollisionChannel> GroundSnapTraceCollisionChannel = ECollisionChannel::ECC_WorldStatic; 
	

	UFUNCTION(BlueprintCallable, Category="Snap to Ground")
	void SnapZoneGraphDataToGround();

	
	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tagging")
	FZoneGraphTag ZoneGraphTagForFreeway;	

	/**
	 * Zone Graph Tag to use for Zone Shapes that are for intersections.
	 * Only used to redundantly tag Zone Shapes. Lane profiles should provide their own per-lane 'pedestrian' tags.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tagging")
	FZoneGraphTag ZoneGraphTagForCrosswalks;	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tagging")
	float GridCellSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tagging")
	int32 NumInterpolationSteps = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tagging")
	float MaxDistanceFromFreewayToCrosswalk = 400.0f;	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tagging")
	float MaxCrosswalkLength = 4000.0f;	
	
	UFUNCTION(BlueprintCallable, Category="Tagging")
	void UntagCrosswalkLanesNearFreewayLaneEndPoints() const;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	float DebugLifetime = 5.0f; 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	float DebugThickness = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	float DebugUpVectorScale = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	AActor* DebugAroundActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	float DebugAroundActorRadius = 10000.0f;

private:

	FCollisionQueryParams CollisionQueryParams;
	FCollisionShape CollisionShape;

	/** Vectors are input, and modified in place. */
	bool SnapPointToGround(UWorld* World, FVector& Point, FVector& UpVector, FVector& TangentVector);
	
	bool IsPointNearActorLocation(const FVector& Point) const;

	void UntagLanesBackToSplitPoint(const TArray<int32>& LaneIndices) const;

};
