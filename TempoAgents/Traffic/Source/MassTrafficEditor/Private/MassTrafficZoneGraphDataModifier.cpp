// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficZoneGraphDataModifier.h"

#include "MassTrafficEditor.h"
#include "WorldPartition/WorldPartition.h"
#include "Misc/ScopedSlowTask.h"
#include "DrawDebugHelpers.h"
#include "ZoneGraphSubsystem.h"
#include "Spatial/PointHashGrid3.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

static const float BigZ = 1000000.0f;


AMassTrafficZoneGraphDataModifier::AMassTrafficZoneGraphDataModifier()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetActorTickEnabled(false);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}


void AMassTrafficZoneGraphDataModifier::BuildZoneGraphData()
{
#if WITH_EDITOR

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - No world."), ANSI_TO_TCHAR(__FUNCTION__));	
		return;
	}

	UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);
	if (!ZoneGraphSubsystem)
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - No ZoneGraphSubsystem."), ANSI_TO_TCHAR(__FUNCTION__));	
		return;
	}

	FZoneGraphBuilder& ZoneGraphBuilder = ZoneGraphSubsystem->GetBuilder();

	TArray<AZoneGraphData*> ZoneGraphDataArray;
	ZoneGraphDataArray.Add(ZoneGraphData);
	
	ZoneGraphBuilder.BuildAll(ZoneGraphDataArray, true);

#endif
}


void AMassTrafficZoneGraphDataModifier::SnapZoneGraphDataToGround()
{
#if WITH_EDITOR

	if (TraceStartZOffset == TraceEndZOffset)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - No world."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	if (!ZoneGraphData)
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - No Zone Graph data."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}


	// Prepare collision.

	CollisionQueryParams.bTraceComplex = GroundSnapTraceComplex;

	if (TraceType == EMassTrafficZoneGraphModifierTraceType::Sphere)
	{
		CollisionShape.SetSphere(TraceSphereRadius);
	}


	// Snap each Zone Graph point to the ground.
	{
		FZoneGraphStorage& ZoneGraphStorage = ZoneGraphData->GetStorageMutable();

		FScopedSlowTask SlowTask(ZoneGraphStorage.Lanes.Num(), NSLOCTEXT("MassTraffic", "SnapZoneGraphDataToGround",
			"Snapping Zone Graph lane points to geometry..."));
		SlowTask.MakeDialog(true);

		FZoneGraphTagFilter ZoneGraphTagFilter;
		{
			ZoneGraphTagFilter.AnyTags = GroundSnapIncludeTags;
			ZoneGraphTagFilter.NotTags = GroundSnapExcludeTags;
		}

		int32 MissCount = 0;
		for (const FZoneLaneData& ZoneLaneData : ZoneGraphStorage.Lanes)
		{
			SlowTask.EnterProgressFrame();
			if (SlowTask.ShouldCancel())
			{
				break;
			}

			if (ZoneGraphTagFilter.Pass(ZoneLaneData.Tags))
			{
				for (int32 PointIndex = ZoneLaneData.PointsBegin; PointIndex < ZoneLaneData.PointsEnd; PointIndex++)
				{
					FVector& LanePoint = ZoneGraphStorage.LanePoints[PointIndex];
					FVector& LaneUpVector = ZoneGraphStorage.LaneUpVectors[PointIndex];
					FVector& LaneTangentVector = ZoneGraphStorage.LaneTangentVectors[PointIndex];
					const bool bHit = SnapPointToGround(World, LanePoint, LaneUpVector, LaneTangentVector);
					if (!bHit)
					{
						++MissCount;
					}
				}
			}
		}

		if (MissCount > 0)
		{
			UE_LOG(LogMassTrafficEditor, Warning, TEXT("%s - %d Zone Graph points could not be snapped to the ground. Use the Trace Debug properties to show these points."),
				ANSI_TO_TCHAR(__FUNCTION__), MissCount);
		}
		else
		{
			UE_LOG(LogMassTrafficEditor, Display, TEXT("%s - All Zone Graph points could be snapped to the ground."),
				ANSI_TO_TCHAR(__FUNCTION__));	
		}
	}
	
#else

	return;
	
#endif
}


bool AMassTrafficZoneGraphDataModifier::SnapPointToGround(UWorld* World, FVector& Point, FVector& UpVector, FVector& TangentVector)
{
	const FVector TraceStart(Point.X, Point.Y, Point.Z + TraceStartZOffset);
	const FVector TraceEnd(Point.X, Point.Y, Point.Z + TraceEndZOffset);
	FHitResult TraceHitResult;
	bool bHit = false;
	if (TraceType == EMassTrafficZoneGraphModifierTraceType::Line)
	{
		bHit = World->LineTraceSingleByChannel(TraceHitResult, TraceStart, TraceEnd, GroundSnapTraceCollisionChannel, CollisionQueryParams);
	}
	else if (TraceType == EMassTrafficZoneGraphModifierTraceType::Sphere)
	{
		bHit = World->SweepSingleByChannel(TraceHitResult, TraceStart, TraceEnd, FQuat::Identity,
			GroundSnapTraceCollisionChannel, CollisionShape, CollisionQueryParams);
	}

	if (bTraceDebugDrawTrace)
	{
		DrawDebugLine(World, TraceStart, TraceEnd, FColor::Silver, false, DebugLifetime, 0, 0.5f * DebugThickness);
	}

	if (bHit)
	{
		const FVector PointOrig = Point;
		
		if (bSnapPointZ)
		{
			Point.Z = TraceHitResult.ImpactPoint.Z + /*optional*/TraceFinalZOffset;
		}
		if (bSnapPointUpVector)
		{
			UpVector = TraceHitResult.ImpactNormal;
			if (bForceUpVectorPositiveZ && UpVector.Z < 0.0f)
			{
				UpVector *= -1.0f;
			}

			const FVector LeftVector = FVector::CrossProduct(UpVector, TangentVector);
			UpVector = FVector::CrossProduct(TangentVector, LeftVector);
		}

		if (bTraceDebugDrawHits && IsPointNearActorLocation(Point))
		{
			DrawDebugDirectionalArrow(World, PointOrig, Point, 10.0f * DebugThickness, FColor::Green, false, DebugLifetime, 0, DebugThickness);

			DrawDebugLine(World, TraceHitResult.ImpactPoint, Point, FColor::Cyan, false, DebugLifetime, 0, DebugThickness / 2.0f);

			if (bSnapPointUpVector)
			{
				DrawDebugDirectionalArrow(World, Point, Point + UpVector * DebugUpVectorScale, 5.0f * DebugThickness, FColor::Yellow, false, DebugLifetime, 0, DebugThickness);
			}
		}
	
		return true;
	}
	else
	{
		if (bTraceDebugDrawMisses && IsPointNearActorLocation(Point))
		{
			// Long upward line, to help the user find the failed trace, since it may be very short or inside of something.
			const FVector TraceDebugStart(TraceStart.X, TraceStart.Y, TraceStartZOffset > 0.0f ? BigZ : -BigZ);
			DrawDebugLine(World, TraceDebugStart, TraceStart, FColor::Yellow, false, DebugLifetime, 0, DebugThickness);

			// Failed trace is shown as an arrow from start to end of trace.
			DrawDebugDirectionalArrow(World, TraceStart, TraceEnd, 10.0f * DebugThickness, FColor::Red, false, DebugLifetime, 0, DebugThickness);

			// Long downward line, to help the user find the failed trace, since it may be very short or inside of something.
			const FVector TraceDebugEnd(TraceEnd.X, TraceEnd.Y, TraceEndZOffset > 0.0f ? BigZ : -BigZ);	
			DrawDebugLine(World, TraceDebugEnd, TraceEnd, FColor::Yellow, false, DebugLifetime, 0, DebugThickness);
		}

		return false;
	}
}


bool AMassTrafficZoneGraphDataModifier::IsPointNearActorLocation(const FVector& Point) const
{
	return (DebugAroundActor ? FVector::Dist(Point, DebugAroundActor->GetTransform().GetLocation()) <= DebugAroundActorRadius : true);
}


void AMassTrafficZoneGraphDataModifier::UntagCrosswalkLanesNearFreewayLaneEndPoints() const
{
	if (NumInterpolationSteps <= 0)
	{
		return;	
	}

	// Only consider valid graph from our world
	if (ZoneGraphData == nullptr || ZoneGraphData->GetWorld() != GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("Zone Graph Data is not set or from our world."));
		return;
	}

	const FZoneGraphStorage& ZoneGraphStorage = ZoneGraphData->GetStorage();

	UE::Geometry::TPointHashGrid3<int32, float> InterpolatedCrosswalkPointGrid(GridCellSize, INDEX_NONE);
	TArray<FVector> InterpolatedCrosswalkPoints;
	TArray<int32> InterpolatedCrosswalkPointLaneIndices;
	int32 ResultIndex = 0;
	{
		for (int32 L = 0; L < ZoneGraphStorage.Lanes.Num(); L++)
		{
			const FZoneLaneData& CrosswalkLaneData = ZoneGraphStorage.Lanes[L];
			if (!CrosswalkLaneData.Tags.Contains(ZoneGraphTagForCrosswalks))
			{
				continue;
			}

			const FVector& CrosswalkLaneStartPoint = ZoneGraphStorage.LanePoints[CrosswalkLaneData.PointsBegin];
			const FVector& CrosswalkLaneEndPoint = ZoneGraphStorage.LanePoints[CrosswalkLaneData.PointsEnd - 1];

			for (int32 I = 0; I < NumInterpolationSteps; I++)
			{
				const float T = static_cast<float>(I) / static_cast<float>(NumInterpolationSteps - 1);
				const FVector InterpolatedCrosswalkPoint = FMath::Lerp(CrosswalkLaneStartPoint, CrosswalkLaneEndPoint, T);
				InterpolatedCrosswalkPointGrid.InsertPoint(ResultIndex, (FVector3f)InterpolatedCrosswalkPoint);
				
				InterpolatedCrosswalkPoints.Add(InterpolatedCrosswalkPoint);
				InterpolatedCrosswalkPointLaneIndices.Add(L);
				++ResultIndex;
			}
		}
	}

	if (!ResultIndex)
	{
		return;
	}

	{
		const float MaxCrosswalkLengthSquared = MaxCrosswalkLength * MaxCrosswalkLength;
		const float MaxDistanceFromFreewayToCrosswalkSquared = MaxDistanceFromFreewayToCrosswalk * MaxDistanceFromFreewayToCrosswalk;
		
		int32 FreewayLaneCount = 0;
		TSet<int32> CrosswalkLaneIndicesAlreadyTagged;
		for (int32 L = 0; L < ZoneGraphStorage.Lanes.Num(); L++)
		{
			const FZoneLaneData& FreewayLaneData = ZoneGraphStorage.Lanes[L];
			if (!FreewayLaneData.Tags.Contains(ZoneGraphTagForFreeway))
			{
				continue;
			}
			
			//UE_LOG(LogTemp, Warning, TEXT("F %d (%d)"), L, FreewayLaneCount);

			auto Check = [&](FVector Point)
			{
				auto DistFunc = [&Point, &InterpolatedCrosswalkPoints](int32 CrosswalkPointID)->float
				{
					return (Point - InterpolatedCrosswalkPoints[CrosswalkPointID]).SquaredLength();
				};
				
				TArray<int32> Results;
				InterpolatedCrosswalkPointGrid.FindPointsInBall((FVector3f)Point, MaxDistanceFromFreewayToCrosswalk, DistFunc, Results);

				for (const int Result : Results)
				{
					if (Result == INDEX_NONE)
					{
						return;
					}
					
					const int32 InterpolatedCrosswalkPointLaneIndex = InterpolatedCrosswalkPointLaneIndices[Result];
					if (CrosswalkLaneIndicesAlreadyTagged.Contains(InterpolatedCrosswalkPointLaneIndex))
					{
						continue;
					}
					
					const FZoneLaneData& CrosswalkLaneData = ZoneGraphStorage.Lanes[InterpolatedCrosswalkPointLaneIndex];
					const FVector CrosswalkLaneStartPoint = ZoneGraphStorage.LanePoints[CrosswalkLaneData.PointsBegin];
					const FVector CrosswalkLaneEndPoint = ZoneGraphStorage.LanePoints[CrosswalkLaneData.PointsEnd - 1];

					const FVector ClosestPointOnCrosswalkLane = FMath::ClosestPointOnSegment(Point, CrosswalkLaneStartPoint, CrosswalkLaneEndPoint);
					if ((ClosestPointOnCrosswalkLane - Point).SquaredLength() > MaxDistanceFromFreewayToCrosswalkSquared)
					{
						return;
					}
					
					if ((CrosswalkLaneEndPoint - CrosswalkLaneStartPoint).SquaredLength() <= MaxCrosswalkLengthSquared)
					{
						return;;
					}

					// UE_LOG(LogTemp, Warning, TEXT("UNTAG LANE %d"), InterpolatedCrosswalkPointLaneIndex);

					// Untag lanes up to wherever the lanes splits.
					TArray<int32> CrosswalkLaneIndicesToUntag;
					CrosswalkLaneIndicesToUntag.Add(InterpolatedCrosswalkPointLaneIndex);
					UntagLanesBackToSplitPoint(CrosswalkLaneIndicesToUntag);
					
					CrosswalkLaneIndicesAlreadyTagged.Add(InterpolatedCrosswalkPointLaneIndex);
				}
			};
			
			Check(ZoneGraphStorage.LanePoints[FreewayLaneData.PointsBegin]);
			Check(ZoneGraphStorage.LanePoints[FreewayLaneData.PointsEnd - 1]);

			++FreewayLaneCount;
		}

		UE_LOG(LogMassTrafficEditor, Warning, TEXT("%s - Untagged %d crosswalk lanes (along with their connecting lanes)"),
			ANSI_TO_TCHAR(__FUNCTION__), CrosswalkLaneIndicesAlreadyTagged.Num());
	}
}


void AMassTrafficZoneGraphDataModifier::UntagLanesBackToSplitPoint(const TArray<int32>& LaneIndices) const
{
	FZoneGraphStorage& ZoneGraphStorage = ZoneGraphData->GetStorageMutable();
	if (LaneIndices.IsEmpty())
	{
		return;	
	}

	for (const int32 ThisLaneIndex : LaneIndices)
	{
		// Remove all tags on this lane.
		FZoneLaneData& ThisLaneData = ZoneGraphStorage.Lanes[ThisLaneIndex];
		ThisLaneData.Tags = FZoneGraphTagMask::None;

		/* DEBUG */
		{
			const FVector ZSmall(0.0f, 0.0f, 20.0f);
			const FVector ZBig(0.0f, 0.0f, 20000.0f);
			
			const FVector LaneStart = ZoneGraphStorage.LanePoints[ThisLaneData.PointsBegin];
			const FVector LaneEnd = ZoneGraphStorage.LanePoints[ThisLaneData.PointsEnd - 1];
			const FVector LaneMid = 0.5f * (LaneStart + LaneEnd);
			
			DrawDebugLine(GetWorld(), LaneStart + ZSmall, LaneEnd + ZSmall, FColor::Red, false, DebugLifetime, 0, DebugThickness);
			DrawDebugLine(GetWorld(), LaneMid + ZSmall, LaneMid + ZBig, FColor::Red, false, DebugLifetime, 0, DebugThickness);
		}
		
		TArray<int32> IncomingLaneIndices;
		bool bThisLaneSplits = false;
		for (int32 LinkIndex = ThisLaneData.LinksBegin; LinkIndex < ThisLaneData.LinksEnd; LinkIndex++)
		{
			const FZoneLaneLinkData& Link = ZoneGraphStorage.LaneLinks[LinkIndex];
			if (Link.Type == EZoneLaneLinkType::Incoming)
			{
				const int32 IncomingLaneIndex = Link.DestLaneIndex;

				IncomingLaneIndices.Add(IncomingLaneIndex);
			}
			if ((static_cast<EZoneLaneLinkFlags>(Link.Flags) & EZoneLaneLinkFlags::Splitting) != EZoneLaneLinkFlags::None)
			{
				bThisLaneSplits = true;
			}
		}

		if (bThisLaneSplits)
		{
			continue;
		}

		UntagLanesBackToSplitPoint(IncomingLaneIndices);
	}
}
