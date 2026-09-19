// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "MassTrafficCrowdLaneEndsProcessor.h"

#include "MassTraffic.h"
#include "MassTrafficSubsystem.h"
#include "MassCommonFragments.h"
#include "MassCrowdFragments.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"

namespace
{
	// What is known about a crowd entity before its lane's tangent has been looked up.
	struct FCrowdLaneEndCandidate
	{
		FMassEntityHandle EntityHandle;
		float DistanceAlongLane = 0.0f;
		FVector Velocity = FVector::ZeroVector;
		FVector Force = FVector::ZeroVector;
		float Radius = 0.0f;
	};

	struct FCrowdLaneEndCandidates
	{
		FCrowdLaneEndCandidate Lead;
		FCrowdLaneEndCandidate Tail;
	};

	FMassTrafficCrowdLaneEndEntity MakeCrowdLaneEndEntity(const FZoneGraphStorage& ZoneGraphStorage, const FZoneGraphLaneHandle& LaneHandle, const FCrowdLaneEndCandidate& Candidate)
	{
		FZoneGraphLaneLocation LaneLocation;
		UE::ZoneGraph::Query::CalculateLocationAlongLane(ZoneGraphStorage, LaneHandle, Candidate.DistanceAlongLane, LaneLocation);

		FMassTrafficCrowdLaneEndEntity EndEntity;
		EndEntity.EntityHandle = Candidate.EntityHandle;
		EndEntity.DistanceAlongLane = Candidate.DistanceAlongLane;
		EndEntity.SpeedAlongLane = FVector::DotProduct(Candidate.Velocity, LaneLocation.Tangent);
		EndEntity.AccelerationAlongLane = FVector::DotProduct(Candidate.Force, LaneLocation.Tangent);
		EndEntity.Radius = Candidate.Radius;
		return EndEntity;
	}
}

UMassTrafficCrowdLaneEndsProcessor::UMassTrafficCrowdLaneEndsProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;

	// Run before the processors that yield vehicles to pedestrians, and pedestrians to vehicles.
	ExecutionOrder.ExecuteBefore.Add(UE::MassTraffic::ProcessorGroupNames::VehicleBehavior);
	ExecutionOrder.ExecuteBefore.Add(UE::MassTraffic::ProcessorGroupNames::CrowdYieldBehavior);
}

void UMassTrafficCrowdLaneEndsProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassForceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);

	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
	ProcessorRequirements.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMassTrafficCrowdLaneEndsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TMap<FZoneGraphLaneHandle, FCrowdLaneEndCandidates> CandidatesByLane;

	EntityQuery.ForEachEntityChunk(Context, [&CandidatesByLane](const FMassExecutionContext& QueryContext)
	{
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityFragments = QueryContext.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FMassForceFragment> ForceFragments = QueryContext.GetFragmentView<FMassForceFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusFragments = QueryContext.GetFragmentView<FAgentRadiusFragment>();

		const int32 NumEntities = QueryContext.GetNumEntities();
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[EntityIndex];

			// Crowd entities that haven't found their lane yet are picked up once they have.
			if (!LaneLocationFragment.LaneHandle.IsValid() || LaneLocationFragment.LaneLength <= 0.0f)
			{
				continue;
			}

			FCrowdLaneEndCandidate Candidate;
			Candidate.EntityHandle = QueryContext.GetEntity(EntityIndex);
			Candidate.DistanceAlongLane = LaneLocationFragment.DistanceAlongLane;
			Candidate.Velocity = VelocityFragments[EntityIndex].Value;
			Candidate.Force = ForceFragments[EntityIndex].Value;
			Candidate.Radius = RadiusFragments[EntityIndex].Radius;

			if (FCrowdLaneEndCandidates* Candidates = CandidatesByLane.Find(LaneLocationFragment.LaneHandle))
			{
				if (Candidate.DistanceAlongLane > Candidates->Lead.DistanceAlongLane)
				{
					Candidates->Lead = Candidate;
				}
				if (Candidate.DistanceAlongLane < Candidates->Tail.DistanceAlongLane)
				{
					Candidates->Tail = Candidate;
				}
			}
			else
			{
				CandidatesByLane.Add(LaneLocationFragment.LaneHandle, { Candidate, Candidate });
			}
		}
	});

	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
	const UZoneGraphSubsystem& ZoneGraphSubsystem = Context.GetSubsystemChecked<UZoneGraphSubsystem>();

	TMap<FZoneGraphLaneHandle, FMassTrafficCrowdLaneEnds> CrowdLaneEnds;
	CrowdLaneEnds.Reserve(CandidatesByLane.Num());
	for (const TPair<FZoneGraphLaneHandle, FCrowdLaneEndCandidates>& LaneCandidates : CandidatesByLane)
	{
		const FZoneGraphLaneHandle& LaneHandle = LaneCandidates.Key;
		const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(LaneHandle.DataHandle);
		if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("Must get valid ZoneGraphStorage in UMassTrafficCrowdLaneEndsProcessor::Execute.")))
		{
			continue;
		}

		FMassTrafficCrowdLaneEnds& LaneEnds = CrowdLaneEnds.Add(LaneHandle);
		LaneEnds.Lead = MakeCrowdLaneEndEntity(*ZoneGraphStorage, LaneHandle, LaneCandidates.Value.Lead);
		LaneEnds.Tail = MakeCrowdLaneEndEntity(*ZoneGraphStorage, LaneHandle, LaneCandidates.Value.Tail);
	}

	MassTrafficSubsystem.SetCrowdLaneEnds(MoveTemp(CrowdLaneEnds));
}
