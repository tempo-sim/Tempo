// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficIntersections.h"
#include "MassTraffic.h"
#include "MassTrafficLights.h"
#include "MassTrafficUtils.h"

#include "ZoneGraphQuery.h" 

/*
 * FMassTrafficIntersectionDetail
 */


/* Min angle between the into-intersection direction of two adjacent sides - for those two sides to think they're part of a square.
 * 90 degrees or less, can be a forgiving angle, like 75 degrees.
 * NOTE - Not static, so I can change it in Live Coding.
 */

/*static*/ const float FMassTrafficIntersectionDetail::MinMostlySquareAdjacentSideAngleDeg = 75.0f;
/*static*/ const float FMassTrafficIntersectionDetail::MaxMostlySquareAdjacentSideCos =
	FMath::Cos(FMath::DegreesToRadians(FMassTrafficIntersectionDetail::MinMostlySquareAdjacentSideAngleDeg));

/* See where this is used. 
 * Min angle between - a vector meant to be along an intersection's side, and - the into-intersection direction.
 * Should be near 90 degrees, slight less to be forgiving, like 75 degrees.
 * NOTE - Seems to make sense to make it the same as MinMostlySquareAdjacentSideAngleDeg.
 *  NOTE - Not static, so I can change it in Live Coding.
 */

/*static*/ const float FMassTrafficIntersectionDetail::MinLaneSideConnectionAngleDeg = MinMostlySquareAdjacentSideAngleDeg;
/*static*/ const float FMassTrafficIntersectionDetail::MaxLaneSideConnectionCos =
	FMath::Cos(FMath::DegreesToRadians(FMassTrafficIntersectionDetail::MinLaneSideConnectionAngleDeg));

/** See where this is used.
 * Min angle between two directions, to check if it's similar to an intersection's side's direction-into-intersection.
 * Used in determining if a direction is part of a hidden (outbound-only) side.
 * Should be an angle close to but less than to 90 degrees. Why? Low lane tesselation means that lanes are composed of
 * longer line segments, and those may and up connecting to an intersection side at a larger angle, usually around 45
 * degrees. We can this angle here even more forgiving, even around 90 degrees. Sides that lanes definitely do NOT
 * connect to will end up connecting with angles 90 degrees or over, giving a 0 or <0 dot product (cosine.)
 * NOTE - Not static, so I can change it in Live Coding.
 */

/*static*/ const float FMassTrafficIntersectionDetail::MinHiddenSideIntoDirectionAngleDeg = 80.0f;
/*static*/ const float FMassTrafficIntersectionDetail::MaxHiddenSideIntoDirectionCos =
	FMath::Cos(FMath::DegreesToRadians(FMassTrafficIntersectionDetail::MinHiddenSideIntoDirectionAngleDeg));



TSet<int32> FindLanesNearPoint_UsingLaneMidpointHGrid(
	const FVector& SearchPoint, const float GridSearchDistance,
	const UE::MassTraffic::FMassTrafficBasicHGrid& SearchLaneMidpointHGrid, const float MaxDistance,
	const float MinLaneLength,
	const FZoneGraphStorage& ZoneGraphStorage)
{
	TSet<int32> OutLaneIndices;
	
	const FVector GridSearchExtent = FVector(GridSearchDistance);

	// Remember, hash grid stores indices for lanes, by their midpoint.
	TArray<int32/*lane index*/> QueryResults;
	SearchLaneMidpointHGrid.Query(FBox::BuildAABB(SearchPoint, GridSearchExtent), QueryResults);
	for (int32 LaneIndex : QueryResults)
	{
		const FVector LaneStartPoint = UE::MassTraffic::GetLaneBeginPoint(LaneIndex, ZoneGraphStorage);
		const FVector LaneEndPoint = UE::MassTraffic::GetLaneEndPoint(LaneIndex, ZoneGraphStorage);
		if (UE::MassTraffic::PointIsNearSegment(SearchPoint, LaneStartPoint, LaneEndPoint, MaxDistance))
		{
			if ((LaneEndPoint - LaneStartPoint).Length() >= MinLaneLength)
			{
				OutLaneIndices.Add(LaneIndex);
			}
		}
	}

	return OutLaneIndices;
}

FMassTrafficIntersectionSide& FMassTrafficIntersectionDetail::AddSide()
{
	bSidesAreOrderedClockwise = false;
	
	const uint32 S = Sides.AddDefaulted(1);
	return Sides[S];
}


bool FMassTrafficIntersectionDetail::IsMostlySquare() const
{
	return
		Sides.Num() == 4 &&
		bSidesAreOrderedClockwise &&
		// In a perfect square, dot products of into-intersection vectors of adjacent sides would be 0.
		// We check for this, but within a tolerance.
		// (Dot product compares adjacency in both orders.)
		FMath::Abs(FVector::DotProduct(Sides[0].DirectionIntoIntersection, Sides[1].DirectionIntoIntersection)) <= MaxMostlySquareAdjacentSideCos &&
		FMath::Abs(FVector::DotProduct(Sides[1].DirectionIntoIntersection, Sides[2].DirectionIntoIntersection)) <= MaxMostlySquareAdjacentSideCos &&
		FMath::Abs(FVector::DotProduct(Sides[2].DirectionIntoIntersection, Sides[3].DirectionIntoIntersection)) <= MaxMostlySquareAdjacentSideCos &&
		FMath::Abs(FVector::DotProduct(Sides[3].DirectionIntoIntersection, Sides[0].DirectionIntoIntersection)) <= MaxMostlySquareAdjacentSideCos;
}


int32 FMassTrafficIntersectionDetail::GetTrafficLanesConnectingSides(int32 StartSideIndex, int32 EndSideIndex, const FZoneGraphStorage& ZoneGraphStorage, TArray<FZoneGraphTrafficLaneData*>& OutTrafficLanes) const
{
	if (StartSideIndex >= Sides.Num() || EndSideIndex >= Sides.Num())
	{
		return 0;
	}

	const FMassTrafficIntersectionSide& BeginSide = Sides[StartSideIndex];
	const FMassTrafficIntersectionSide& EndSide = Sides[EndSideIndex];

	for (const auto& Elem : BeginSide.VehicleIntersectionLanes)
	{
		FZoneGraphTrafficLaneData* StartInboundTrafficLaneData = Elem.Key;
		const FVector DirectionAlongEndSide = (UE::MassTraffic::GetLaneEndPoint(StartInboundTrafficLaneData->LaneHandle.Index, ZoneGraphStorage) - EndSide.IntersectionLanesBeginMidpoint).GetSafeNormal();
		if (FVector::DotProduct(EndSide.DirectionIntoIntersection, DirectionAlongEndSide) <= MaxLaneSideConnectionCos)
		{
			OutTrafficLanes.Add(StartInboundTrafficLaneData);
		}
	}
		
	return OutTrafficLanes.Num();
}


bool FMassTrafficIntersectionDetail::HasSideWithInboundLanesFromFreeway() const
{
	for (int32 S = 0; S < Sides.Num(); S++)
	{
		if (Sides[S].bHasInboundLanesFromFreeway)
		{
			return true;
		}
	}

	return false;
}


// See 'NOTE ON HIDDEN SIDES'
bool FMassTrafficIntersectionDetail::HasHiddenSides() const
{
	return HiddenOutboundSideHints.Points.Num() > 0;	
}


void FMassTrafficIntersectionDetail::Build(
	const UE::MassTraffic::FMassTrafficBasicHGrid& CrosswalkLaneMidpoint_HGrid, const float IntersectionSideToCrosswalkSearchDistance, 
	const UE::MassTraffic::FMassTrafficBasicHGrid& TrafficLightIntersectionSideHGrid, const TArray<FMassTrafficLightInstanceDesc>* TrafficLightInstanceDescs, float TrafficLightSearchDistance,
	const UE::MassTraffic::FMassTrafficBasicHGrid& TrafficSignIntersectionSideHGrid, const TArray<FMassTrafficSignInstanceDesc>* TrafficSignInstanceDescs, float TrafficSignSearchDistance,
	const FZoneGraphStorage& ZoneGraphStorage,
	const UWorld& World)
{
	// Calculate and store -
	//		(1) The midpoint and into-intersection-direction of each intersection side.
	//		(2) The center point for each intersection.
	{
		SidesCenter = FVector::ZeroVector;
		float SideCount = 0.0f;
		for (FMassTrafficIntersectionSide& Side : Sides)
		{
			FVector Midpoint = FVector::ZeroVector;
			FVector DirectionIntoIntersection = FVector::ZeroVector;
			float Count = 0.0f;
			for (const auto& Elem : Side.VehicleIntersectionLanes)
			{
				const FMassTrafficIntersectionSideLaneInfo& SideLaneInfo = Elem.Value;
				Midpoint += SideLaneInfo.EntranceLocation;
				DirectionIntoIntersection += SideLaneInfo.EntranceDirection;
				Count += 1.0f;
			}
			Midpoint /= Count;
			DirectionIntoIntersection /= Count;
					
			Side.IntersectionLanesBeginMidpoint = Midpoint;
			Side.DirectionIntoIntersection = DirectionIntoIntersection;

			SidesCenter += Midpoint;
			SideCount += 1.0f;
		}

		SidesCenter /= SideCount;
	}

	
	// Re-order the intersection sides to be clockwise.
	// Why? 
	//		(1) Ensures a nice cycling behavior.
	//		(2) Required for building periods for four-sided bi-directional traffic.
	{
		TArray<FMassTrafficFloatAndID> ZAngleAndSideIndexArray;
		for (int32 S = 0; S < Sides.Num(); S++)
		{
			const FVector SideDirection = Sides[S].DirectionIntoIntersection;

			static const FVector ReferenceDirection(1.0f, 0.0f, 0.0f);
			const float Dot = FVector::DotProduct(ReferenceDirection, SideDirection);
			const FVector Cross = FVector::CrossProduct(ReferenceDirection, SideDirection);	

			const float SortSign = (Cross.Z > 0.0f ? 1.0f : -1.0f);
			const float ZAngle = FMath::Acos(Dot);
			const float SignedZAngle = SortSign * ZAngle;

			ZAngleAndSideIndexArray.Add(FMassTrafficFloatAndID(SignedZAngle, S));
		}
	
		ZAngleAndSideIndexArray.Sort();

		TArray<FMassTrafficIntersectionSide> OldSides = Sides;
		Sides.Empty();
		for (int32 S = 0; S < ZAngleAndSideIndexArray.Num(); S++)
		{
			const int32 SOld = ZAngleAndSideIndexArray[S].ID;
			Sides.Add(OldSides[SOld]);
		}

		bSidesAreOrderedClockwise = true;
	}


	// See 'NOTE ON HIDDEN SIDES'
	// Collect side points and into-intersection-directions for hidden (outbound-only) sides, if any.
	{
		HiddenOutboundSideHints.Points.Empty();
		HiddenOutboundSideHints.DirectionsIntoIntersection.Empty();

		const int32 NumSides = Sides.Num();
		for (int32 SourceSideIndex = 0; SourceSideIndex < NumSides; SourceSideIndex++)
		{
			const FMassTrafficIntersectionSide& SourceSide = Sides[SourceSideIndex];
			for (const auto& Elem : SourceSide.VehicleIntersectionLanes)
			{
				FZoneGraphTrafficLaneData* SourceSideIntersectionTrafficLaneData = Elem.Key;
				const FVector& SourceSideIntersectionTrafficLaneData_EndPoint = UE::MassTraffic::GetLaneEndPoint(SourceSideIntersectionTrafficLaneData->LaneHandle.Index, ZoneGraphStorage);
				const FVector& SourceSideIntersectionTrafficLaneData_EndDirectionIntoIntersection = -UE::MassTraffic::GetLaneEndDirection(SourceSideIntersectionTrafficLaneData->LaneHandle.Index, ZoneGraphStorage);

				bool bTrafficLaneDataEndsOnKnownSide = false;
				for (int32 PossibleDestinationSideIndex = 0; PossibleDestinationSideIndex < NumSides; PossibleDestinationSideIndex++)
				{
					if (SourceSideIndex == PossibleDestinationSideIndex) continue;

					const FMassTrafficIntersectionSide& PossibleDestinationSide = Sides[PossibleDestinationSideIndex];
					const float Dot = FVector::DotProduct(SourceSideIntersectionTrafficLaneData_EndDirectionIntoIntersection, PossibleDestinationSide.DirectionIntoIntersection);
					if (Dot >= MaxHiddenSideIntoDirectionCos)
					{
						bTrafficLaneDataEndsOnKnownSide = true;
						break; 
					}
				}

				if (!bTrafficLaneDataEndsOnKnownSide)
				{
					HiddenOutboundSideHints.Points.Add(SourceSideIntersectionTrafficLaneData_EndPoint);
					HiddenOutboundSideHints.DirectionsIntoIntersection.Add(SourceSideIntersectionTrafficLaneData_EndDirectionIntoIntersection);
				}
			}
		}
	}
 
	// See 'NOTE ON HIDDEN SIDES'
	// Link sides to the pedestrian lanes they cross.
	{
		// Lamda
		// For all the lanes in LaneIndices, find out which lanes are incoming to them, and add those to
		// LaneIndices too. These end up being pedestrian intersection lanes.
		auto AddIncomingLaneIndices = [&](const TSet<int32>& InLaneIndices, TSet<int32>& OutLaneIndices)->void
		{
			TSet<int32> LaneIndicesToAdd;
			for (int32 LaneIndex : InLaneIndices)
			{
				// Added by Yoan.
				// Retrieve all incoming lanes to close them too.
				TArray<FZoneGraphLinkedLane> Links;
				UE::ZoneGraph::Query::GetLinkedLanes(ZoneGraphStorage, LaneIndex, EZoneLaneLinkType::Incoming, EZoneLaneLinkFlags::All, EZoneLaneLinkFlags::None, Links);
				for (FZoneGraphLinkedLane& Link : Links)
				{
					LaneIndicesToAdd.Add(Link.DestLane.Index);
				}
			}

			OutLaneIndices.Append(LaneIndicesToAdd);
		};


		// Find all pedestrian lane indices that are crossed by lanes GOING INTO a hidden (outbound-only) side, if any.
		// ADDED BY YOAN: Also add all pedestrian lanes that are leading into those (added by Yoan.)
		if (HasHiddenSides())
		{
			const int32 NumHiddenSidePoints = HiddenOutboundSideHints.Points.Num();
			for (int32 I = 0; I < NumHiddenSidePoints; I++)
			{
				const FVector& Point = HiddenOutboundSideHints.Points[I];

				TSet<int32> CrosswalkLaneIndices = FindLanesNearPoint_UsingLaneMidpointHGrid(
					Point, (Point - SidesCenter).Length() /*grid search size*/,
					CrosswalkLaneMidpoint_HGrid, IntersectionSideToCrosswalkSearchDistance,
					0.0f,
					ZoneGraphStorage);
	
				HiddenOutboundSideHints.CrosswalkLanes.Append(CrosswalkLaneIndices);
			}

			// Yes, outside loop.
			AddIncomingLaneIndices(HiddenOutboundSideHints.CrosswalkLanes,HiddenOutboundSideHints.CrosswalkWaitingLanes);
		}
		
		// Find all pedestrian lane indices that are crossed by incoming lanes coming COMING OUT FROM this intersection side.
		// ADDED BY YOAN: Also add all pedestrian lanes that are leading into those (added by Yoan.)
		{
			for (FMassTrafficIntersectionSide& Side : Sides)
			{
				const FVector& Point = Side.IntersectionLanesBeginMidpoint;

				TSet<int32> CrosswalkLaneIndices = FindLanesNearPoint_UsingLaneMidpointHGrid(
					Point, (Point - SidesCenter).Length() /*grid search size*/,
					CrosswalkLaneMidpoint_HGrid, IntersectionSideToCrosswalkSearchDistance,
					0.0f,
					ZoneGraphStorage);
	
				Side.CrosswalkLanes.Append(CrosswalkLaneIndices);

				// Yes, inside loop.
				AddIncomingLaneIndices(Side.CrosswalkLanes,Side.CrosswalkWaitingLanes);
			}
		}
	}


	// Find the traffic light instance descs that control intersection lanes on intersection sides.
	{
		bHasTrafficLights = false;

		for (FMassTrafficIntersectionSide& Side : Sides)
		{
			// Crosswalks on roads are always treated as yields.
			if (bIsRoadCrosswalk)
			{
				Side.TrafficControllerSignType = EMassTrafficControllerSignType::YieldSign;
				Side.TrafficLightInstanceDescIndex = INDEX_NONE;
				continue;
			}
			// Find the left-most (closest to road center) intersection lane begin point. It will represent this
			// intersection side in searches for the traffic light that controls it, which is in the middle of the
			// intersection side. (This point should be closest to that.)
			FVector LeftMostIntersectionLaneBeginPoint;
			{
				float FarthestDistance = TNumericLimits<float>::Lowest();
				bool bFoundLeftMostIntersectionLaneBeginPoint = false;
				for (const auto& Elem : Side.VehicleIntersectionLanes)
				{
					FZoneGraphTrafficLaneData* IntersectionTrafficLaneData = Elem.Key;
					const FVector IntersectionLaneBeginPoint = UE::MassTraffic::GetLaneBeginPoint(IntersectionTrafficLaneData->LaneHandle.Index, ZoneGraphStorage);
					const FVector FromPoint_ToIntersectionLanesBeginMidpoint_Direction = IntersectionLaneBeginPoint - Side.IntersectionLanesBeginMidpoint;

					const FVector Cross = FVector::CrossProduct(FromPoint_ToIntersectionLanesBeginMidpoint_Direction, Side.DirectionIntoIntersection);
					const float Distance = FVector::Distance(IntersectionLaneBeginPoint, Side.IntersectionLanesBeginMidpoint);
					if ((Cross.Z < 0.0f && Distance > 1.0f/*1 cm*/) || Distance < FarthestDistance)
					{
						continue;
					}

					LeftMostIntersectionLaneBeginPoint = IntersectionLaneBeginPoint;
					FarthestDistance = Distance;
					bFoundLeftMostIntersectionLaneBeginPoint = true;
				}

				if (!bFoundLeftMostIntersectionLaneBeginPoint)
				{
					UE_LOG(LogMassTraffic, Error, TEXT("%s - Intersection %d has side with no left most intersection lane"), ANSI_TO_TCHAR(__FUNCTION__), ZoneIndex);
					continue;
				}
			}
			
			// Hash grid stores indices for traffic light instance descs, by their controlled intersection side midpoint.
			// (This controlled intersection side is the 'real' midpoint of the side, both inbound and outbound lanes.)
			// Look for any of those that are close to the left-most intersection lanes begin point, which should be the
			// point closest to the center of the road. We don't bother looking further than a certain distance. 
			const FVector TrafficLightQueryExtent = FVector(TrafficLightSearchDistance);
			TArray<int32/*traffic light instance desc index*/> TrafficLightQueryResults;
			TrafficLightIntersectionSideHGrid.Query(FBox::BuildAABB(LeftMostIntersectionLaneBeginPoint, TrafficLightQueryExtent), TrafficLightQueryResults);

			// Find nearest traffic light instance desc - by comparing distances between (1) each traffic light instance desc's
			// intersection side midpoint, of the side it controls (2) this intersection side's intersection left-most
			// lane's begin point - which should be the point closest to (1) and the center of the road.
			float NearestTrafficLightInstanceDescDistance = TNumericLimits<float>::Max();
			int32 NearestTrafficLightInstanceDescIndex = INDEX_NONE;
			if (TrafficLightInstanceDescs != nullptr)
			{
				for (int32 TrafficLightInstanceDescIndex : TrafficLightQueryResults)
				{
					const FMassTrafficLightInstanceDesc& TrafficLightInstanceDesc = (*TrafficLightInstanceDescs)[TrafficLightInstanceDescIndex];
					const float Distance = (LeftMostIntersectionLaneBeginPoint - TrafficLightInstanceDesc.ControlledIntersectionSideMidpoint).Length();
					if (Distance < NearestTrafficLightInstanceDescDistance)
					{
						NearestTrafficLightInstanceDescDistance = Distance;
						NearestTrafficLightInstanceDescIndex = TrafficLightInstanceDescIndex;
					}
				}
			}

			Side.TrafficLightInstanceDescIndex = NearestTrafficLightInstanceDescIndex; // ..may be INDEX_NONE
			bHasTrafficLights |= (NearestTrafficLightInstanceDescIndex != INDEX_NONE);
			
			const FVector TrafficSignQueryExtent = FVector(TrafficSignSearchDistance);
			TArray<int32/*traffic sign instance desc index*/> TrafficSignQueryResults;
			TrafficSignIntersectionSideHGrid.Query(FBox::BuildAABB(LeftMostIntersectionLaneBeginPoint, TrafficSignQueryExtent), TrafficSignQueryResults);

			float NearestTrafficSignInstanceDescDistance = TNumericLimits<float>::Max();
			int32 NearestTrafficSignInstanceDescIndex = INDEX_NONE;
			
			if (TrafficSignInstanceDescs != nullptr)
			{
				for (int32 TrafficSignInstanceDescIndex : TrafficSignQueryResults)
				{
					const FMassTrafficSignInstanceDesc& TrafficSignInstanceDesc = (*TrafficSignInstanceDescs)[TrafficSignInstanceDescIndex];
					const float Distance = (LeftMostIntersectionLaneBeginPoint - TrafficSignInstanceDesc.ControlledIntersectionSideMidpoint).Length();
					if (Distance < NearestTrafficSignInstanceDescDistance)
					{
						NearestTrafficSignInstanceDescDistance = Distance;
						NearestTrafficSignInstanceDescIndex = TrafficSignInstanceDescIndex;
					}
				}
			}

			if (NearestTrafficSignInstanceDescIndex != INDEX_NONE)
			{
				const FMassTrafficSignInstanceDesc& NearestTrafficSignInstanceDesc = (*TrafficSignInstanceDescs)[NearestTrafficSignInstanceDescIndex];
				Side.TrafficControllerSignType = NearestTrafficSignInstanceDesc.TrafficControllerSignType;
			}
			else
			{
				Side.TrafficControllerSignType = EMassTrafficControllerSignType::None;
			}
		}
	}
}
