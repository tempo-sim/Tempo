// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficDriverVisualizationProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficDrivers.h"
#include "MassTrafficFragments.h"
#include "MassTrafficVehicleInterface.h"

#include "AnimToTextureDataAsset.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"


UMassTrafficDriverVisualizationProcessor::UMassTrafficDriverVisualizationProcessor()
	: EntityQuery_Conditional(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true; // due to RW access to FMassRepresentationSubsystemSharedFragment
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::PostPhysicsDriverVisualization;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PostPhysicsUpdateTrafficVehicles);
}

void UMassTrafficDriverVisualizationProcessor::ConfigureQueries()
{
	// No need to call super as we do not use it's LOD calculation code at all.
	EntityQuery_Conditional.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	EntityQuery_Conditional.AddRequirement<FMassViewerInfoFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficDriverVisualizationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleDamageFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddConstSharedRequirement<FMassTrafficDriversParameters>();
	EntityQuery_Conditional.AddSharedRequirement<FMassRepresentationSubsystemSharedFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
}

void UMassTrafficDriverVisualizationProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	World = Owner.GetWorld();
}

void UMassTrafficDriverVisualizationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Skip driver vis?
	if (!GMassTrafficDrivers)
	{
		return;
	}
	
	// Draw vehicle drivers
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DriverVisualization"))

	check(World);
	const float GlobalTime = World->GetTimeSeconds();

	// Grab player's spatial data (assume single player)
	FVector PlayerMeshLocation = FVector::ZeroVector;
	if (const ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
		if (const USkeletalMeshComponent* PlayerMesh = PlayerChar->GetMesh())
		{
			PlayerMeshLocation = PlayerMesh->GetComponentLocation();
		}
	}

	EntityQuery_Conditional.ForEachEntityChunk(EntityManager, Context, [&, this](FMassExecutionContext& QueryContext)
	{
		// Get mutable ISMInfos to append instances & custom data to
		UMassRepresentationSubsystem* RepresentationSubsystem = Context.GetMutableSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
		check(RepresentationSubsystem);
		FMassInstancedStaticMeshInfoArrayView ISMInfo = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

		const FMassTrafficDriversParameters& Params = QueryContext.GetConstSharedFragment<FMassTrafficDriversParameters>();
	
		float MaxDriverVisualizationDistanceSq = GMassTrafficMaxDriverVisualizationDistance >= 0.0f ? FMath::Square(GMassTrafficMaxDriverVisualizationDistance) : FLT_MAX;

		const int32 NumEntities = QueryContext.GetNumEntities();
		TArrayView<FMassRepresentationFragment> RepresentationFragments = QueryContext.GetMutableFragmentView<FMassRepresentationFragment>();
		const TConstArrayView<FMassViewerInfoFragment> ViewerInfoFragments = QueryContext.GetFragmentView<FMassViewerInfoFragment>();
		const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODFragments = QueryContext.GetFragmentView<FMassRepresentationLODFragment>();
		const TConstArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = QueryContext.GetFragmentView<FMassTrafficVehicleControlFragment>();
		const TConstArrayView<FMassTrafficVehicleDamageFragment> VehicleDamageFragments = QueryContext.GetFragmentView<FMassTrafficVehicleDamageFragment>();
		const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = QueryContext.GetFragmentView<FMassTrafficRandomFractionFragment>();
		const TConstArrayView<FTransformFragment> TransformFragments = QueryContext.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassTrafficPIDVehicleControlFragment> PIDVehicleControlFragments = QueryContext.GetFragmentView<FMassTrafficPIDVehicleControlFragment>();
		TArrayView<FMassTrafficDriverVisualizationFragment> DriverVisualizationFragments = QueryContext.GetMutableFragmentView<FMassTrafficDriverVisualizationFragment>();
		TArrayView<FMassActorFragment> ActorFragments = QueryContext.GetMutableFragmentView<FMassActorFragment>();
		for (int32 EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
		{
			FMassTrafficDriverVisualizationFragment& DriverVisualizationFragment = DriverVisualizationFragments[EntityIdx];
			if (DriverVisualizationFragment.DriverTypeIndex == FMassTrafficDriverVisualizationFragment::InvalidDriverTypeIndex)
			{
				continue;
			}
		
			FMassRepresentationFragment& RepresentationFragment = RepresentationFragments[EntityIdx];
			const FMassViewerInfoFragment& ViewerInfoFragment = ViewerInfoFragments[EntityIdx];
			const FMassRepresentationLODFragment& RepresentationLODFragment = RepresentationLODFragments[EntityIdx];
			const FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[EntityIdx];
			const FMassTrafficVehicleDamageFragment& VehicleDamageFragment = VehicleDamageFragments[EntityIdx];
			const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[EntityIdx];
			const FTransformFragment& TransformFragment = TransformFragments[EntityIdx];
		
			// Draw drivers in medium viewer LOD vehicles using FStaticMeshInstanceVisualizationDesc::TransformOffset
			// as the relative drivers seat offset
			check(IsValid(Params.DriverTypesData));
			const FMassTrafficDriverTypeData& DriverType = Params.DriverTypesData->DriverTypes[DriverVisualizationFragment.DriverTypeIndex];

			const FStaticMeshInstanceVisualizationDescHandle DriverStaticMeshDescHandle = Params.DriverTypesStaticMeshDescHandle[DriverVisualizationFragment.DriverTypeIndex];
			if (RepresentationLODFragment.LOD <= GMassTrafficMaxDriverVisualizationLOD && ViewerInfoFragment.ClosestViewerDistanceSq <= MaxDriverVisualizationDistanceSq && DriverStaticMeshDescHandle.IsValid())
			{
				const FTransform DriverTransform = Params.DriversSeatOffset * TransformFragment.GetTransform();
				const FTransform DriverPrevTransform = Params.DriversSeatOffset * RepresentationFragment.PrevTransform;
				RepresentationFragment.PrevTransform = TransformFragment.GetTransform();

				if (const UAnimToTextureDataAsset* AnimData = DriverType.AnimationData.Get())
				{
					// Explicit anim state variation from DriverType e.g: force bus variation? 
					EDriverAnimStateVariation AnimStateVariation;
					if (Params.AnimStateVariationOverride != EDriverAnimStateVariation::None)
					{
						AnimStateVariation = Params.AnimStateVariationOverride;
					}
					else
					{
						// Otherwise randomly choose One or Two handed driving
						AnimStateVariation = RandomFractionFragment.RandomFraction <= AlternateDrivingStanceRatio ? 
								EDriverAnimStateVariation::OneHand : EDriverAnimStateVariation::TwoHands;
					}
							
					const int32 AnimStateVariationIndex = static_cast<int32>(AnimStateVariation);
					FMassTrafficInstancePlaybackData CustomData;
					const float SteeringInput = PIDVehicleControlFragments.IsEmpty() ? 0.0f : PIDVehicleControlFragments[EntityIdx].Steering;
					if (SteeringInput >= -PlaybackSteeringThreshold && SteeringInput <= PlaybackSteeringThreshold)
					{
						if (VehicleControlFragment.Speed > LowSpeedThreshold)
						{
							DriverVisualizationFragment.AnimState = ETrafficDriverAnimState::HighSpeedIdle;
							DriverVisualizationFragment.AnimStateGlobalTime = -RandomFractionFragment.RandomFraction * 10.0f;
							PopulateAnimPlaybackFromAnimState(
								AnimData,
								static_cast<int32>(DriverVisualizationFragment.AnimState),
								AnimStateVariationIndex,
								DriverVisualizationFragment.AnimStateGlobalTime,
								CustomData);
						}
						else
						{
							const FVector DriverToPlayer = PlayerMeshLocation - DriverTransform.GetLocation();
							const float DriverToPlayerSizeSqrd = DriverToPlayer.SizeSquared();
							bool bIsLookIdle = false;

							if (DriverToPlayerSizeSqrd < LookIdleMinDistSqrd)
							{
								const FVector DriverToPlayerDir = DriverToPlayer.GetSafeNormal();
								const FVector DriverLeftDir = DriverTransform.GetUnitAxis(EAxis::X);
								const float LeftDirDotToPlayer = FVector::DotProduct(DriverLeftDir, DriverToPlayerDir);
								if (FMath::Abs(LeftDirDotToPlayer) >= LookIdleMinDotToPlayer)
								{
									ETrafficDriverAnimState NewState =
										LeftDirDotToPlayer >= 0.0f ?
										ETrafficDriverAnimState::LookLeftIdle :
										ETrafficDriverAnimState::LookRightIdle;

									if (NewState != DriverVisualizationFragment.AnimState)
									{
										DriverVisualizationFragment.AnimState = NewState;
										DriverVisualizationFragment.AnimStateGlobalTime = GlobalTime;
									}
									PopulateAnimPlaybackFromAnimState(
										AnimData,
										static_cast<int32>(DriverVisualizationFragment.AnimState),
										AnimStateVariationIndex,
										DriverVisualizationFragment.AnimStateGlobalTime,
										CustomData);
									bIsLookIdle = true;
								}
							}

							if (!bIsLookIdle)
							{
								DriverVisualizationFragment.AnimState = ETrafficDriverAnimState::LowSpeedIdle;
								DriverVisualizationFragment.AnimStateGlobalTime = -RandomFractionFragment.RandomFraction * 10.0f;
								PopulateAnimPlaybackFromAnimState(
									AnimData,
									static_cast<int32>(DriverVisualizationFragment.AnimState),
									AnimStateVariationIndex,
									DriverVisualizationFragment.AnimStateGlobalTime,
									CustomData);
							}
						}
					}
					else
					{
						DriverVisualizationFragment.AnimState = ETrafficDriverAnimState::Steering;
						PopulateAnimEvalFromAnimState(
							AnimData,
							static_cast<int32>(DriverVisualizationFragment.AnimState),
							AnimStateVariationIndex,
							SteeringInput,
							FFloatInterval(-1.0f, 1.0f),
							CustomData);
					}

					// Remove the driver if vehicle is damaged
					const bool bRemoveDriver = static_cast<int32>(VehicleDamageFragment.VehicleDamageState) >= static_cast<int32>(RemoveDriverDamageThreshold);
					if (bRemoveDriver)
					{
						FMassActorFragment& ActorFragment = ActorFragments[EntityIdx];
						AActor* Actor = ActorFragment.GetMutable();
						bool bActorImplementsTrafficVehicleInterface = IsValid(Actor) ? Actor->Implements<UMassTrafficVehicleInterface>() : false;
						if (bActorImplementsTrafficVehicleInterface)
						{
							QueryContext.Defer().PushCommand<FMassDeferredSetCommand>([=](FMassEntityManager&)
							{
								if (IsValid(Actor))
								{
									IMassTrafficVehicleInterface::Execute_OnDriverRemoved(Actor, DriverType, CustomData, DriverTransform);
								}
							});
						}

						// Remove the driver from damaged vehicles by invalidating the driver type index so it
						// gets skipped for visualization. A new driver will be reassigned if this vehicle gets
						// recycled
						DriverVisualizationFragment.DriverTypeIndex = FMassTrafficDriverVisualizationFragment::InvalidDriverTypeIndex; 
						continue;
					}
					else
					{
						ISMInfo[DriverStaticMeshDescHandle.ToIndex()].AddBatchedTransform(QueryContext.GetEntity(EntityIdx)
							, DriverTransform, DriverPrevTransform, RepresentationLODFragment.LODSignificance);
						ISMInfo[DriverStaticMeshDescHandle.ToIndex()].AddBatchedCustomData(CustomData, RepresentationLODFragment.LODSignificance);
					}
				}
			}
		}
	});
}

bool UMassTrafficDriverVisualizationProcessor::PopulateAnimEvalFromAnimState(
	const UAnimToTextureDataAsset* AnimData,
	int32 StateIndex,
	int32 VariationIndex,
	float EvalInput,
	const FFloatInterval& InputInterval,
	FMassTrafficInstancePlaybackData& OutPlaybackData)
{
	if (PopulateAnimFromAnimState(AnimData, StateIndex, VariationIndex, OutPlaybackData))
	{
		const float Ratio = (EvalInput - InputInterval.Min) / (InputInterval.Max - InputInterval.Min);
		const int32 EvaluateAnimFrame = FMath::RoundToFloat(Ratio * (OutPlaybackData.CurrentState.NumFrames - 1.0f));
		OutPlaybackData.CurrentState.StartFrame += EvaluateAnimFrame;
		OutPlaybackData.CurrentState.NumFrames = 1;
		return true;
	}

	return false;
}

bool UMassTrafficDriverVisualizationProcessor::PopulateAnimPlaybackFromAnimState(
	const UAnimToTextureDataAsset* AnimData,
	int32 StateIndex,
	int32 VariationIndex,
	float GlobalStartTime,
	FMassTrafficInstancePlaybackData& OutPlaybackData)
{
	if (PopulateAnimFromAnimState(AnimData, StateIndex, VariationIndex, OutPlaybackData))
	{
		OutPlaybackData.CurrentState.GlobalStartTime = GlobalStartTime;
		return true;
	}

	return false;
}

bool UMassTrafficDriverVisualizationProcessor::PopulateAnimFromAnimState(
	const UAnimToTextureDataAsset* AnimData,
	int32 StateIndex,
	int32 VariationIndex,
	FMassTrafficInstancePlaybackData& OutPlaybackData)
{
	const int VariationAnimStateIndex = static_cast<int32>(ETrafficDriverAnimState::Count) * VariationIndex + StateIndex;
	if (AnimData && AnimData->Animations.IsValidIndex(VariationAnimStateIndex))
	{

		const FAnimToTextureAnimInfo& AnimInfo = AnimData->Animations[VariationAnimStateIndex];
		OutPlaybackData.CurrentState.StartFrame = AnimInfo.StartFrame;
		OutPlaybackData.CurrentState.NumFrames = AnimInfo.EndFrame - AnimInfo.StartFrame + 1; // AnimInfo.NumFrames;
		OutPlaybackData.CurrentState.bLooping = true; // AnimInfo.bLooping;

		return true;
	}

	return false;
}

