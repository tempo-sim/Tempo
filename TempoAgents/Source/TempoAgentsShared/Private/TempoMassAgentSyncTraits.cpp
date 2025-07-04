// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMassAgentSyncTraits.h"

#include "MassEntityTemplateRegistry.h"
#include "MassExecutionContext.h"
#include "MassTrafficFragments.h"
#include "TempoBrightnessMeter.h"
#include "TempoBrightnessTranslator.h"
#include "Kismet/GameplayStatics.h"

//----------------------------------------------------------------------//
//  UTempoMassAgentBrightnessMeterSyncTrait
//----------------------------------------------------------------------//
void UTempoMassAgentBrightnessMeterSyncTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FMassBrightnessMeterWrapperFragment>();
	BuildContext.AddFragment<FEnvironmentalBrightnessFragment>();

	BuildContext.GetMutableObjectFragmentInitializers().Add([=, &World](UObject& Owner, FMassEntityView& EntityView, const EMassTranslationDirection CurrentDirection)
		{
			const ATempoBrightnessMeter* BrightnessMeterActor = Cast<ATempoBrightnessMeter>(UGameplayStatics::GetActorOfClass(&World, ATempoBrightnessMeter::StaticClass()));
			if (BrightnessMeterActor == nullptr)
			{
				return;
			}

			// Note:  When Mass considers its Actor representations to be puppets,
			// it only initializes them with CurrentDirection == EMassTranslationDirection::MassToActor.
			// However, in this case, Mass needs information from the Actor in order to make decisions.
			// For example, Mass needs to know about the environmental brightness in order to control
			// the vehicles' headlights.

			UTempoBrightnessMeterComponent* BrightnessMeterComponent = BrightnessMeterActor->BrightnessMeterComponent;
			if (!ensureMsgf(BrightnessMeterComponent != nullptr, TEXT("UTempoMassAgentBrightnessMeterSyncTrait expects BrightnessMeterActor to have a valid UTempoBrightnessMeterComponent.")))
			{
				return;
			}
			
			FMassBrightnessMeterWrapperFragment& BrightnessMeterWrapperFragment = EntityView.GetFragmentData<FMassBrightnessMeterWrapperFragment>();
			BrightnessMeterWrapperFragment.BrightnessMeterComponent = BrightnessMeterComponent;
				
			FEnvironmentalBrightnessFragment& EnvironmentalBrightnessFragment = EntityView.GetFragmentData<FEnvironmentalBrightnessFragment>();
			EnvironmentalBrightnessFragment.Brightness = BrightnessMeterComponent->GetBrightness();
		});

	if (EnumHasAnyFlags(SyncDirection, EMassTranslationDirection::ActorToMass))
	{
		BuildContext.AddTranslator<UMassBrightnessMeterToMassTranslator>();
	}

	if (EnumHasAnyFlags(SyncDirection, EMassTranslationDirection::MassToActor))
	{
		ensureMsgf(false, TEXT("Actor must be the authority.  Entity as the authority is not currently supported."));
	}
}

//----------------------------------------------------------------------//
// UTempoMassSceneComponentTransformToMassTranslator
//----------------------------------------------------------------------//
UTempoMassSceneComponentTransformToMassTranslator::UTempoMassSceneComponentTransformToMassTranslator()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	RequiredTags.Add<FTempoMassTransformCopyToMassTag>();
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UTempoMassSceneComponentTransformToMassTranslator::ConfigureQueries()
#else
void UTempoMassSceneComponentTransformToMassTranslator::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#endif
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FSceneComponentWrapperFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
}

void UTempoMassSceneComponentTransformToMassTranslator::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
#else
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
#endif
		{
			const TConstArrayView<FSceneComponentWrapperFragment> CapsuleComponentList = Context.GetFragmentView<FSceneComponentWrapperFragment>();
			const TArrayView<FTransformFragment> LocationList = Context.GetMutableFragmentView<FTransformFragment>();
			for (int i = 0; i < CapsuleComponentList.Num(); ++i)
			{
				if (const USceneComponent* SceneComp = CapsuleComponentList[i].Component.Get())
				{
					LocationList[i].GetMutableTransform() = SceneComp->GetComponentTransform();
				}
			}
		});
}

//----------------------------------------------------------------------//
//  UMassAgentTransformSyncTrait
//----------------------------------------------------------------------//
void UMassAgentTransformSyncTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FSceneComponentWrapperFragment>();
	BuildContext.RequireFragment<FTransformFragment>();

	BuildContext.GetMutableObjectFragmentInitializers().Add([this](UObject& Owner, FMassEntityView& EntityView, const EMassTranslationDirection CurrentDirection)
		{
			if (const AActor* Actor = Cast<AActor>(&Owner))
			{
				if (USceneComponent* SceneComponent = Actor->GetRootComponent())
				{
					FSceneComponentWrapperFragment& SceneComponentFragment = EntityView.GetFragmentData<FSceneComponentWrapperFragment>();
					SceneComponentFragment.Component = SceneComponent;

					FTransformFragment& TransformFragment = EntityView.GetFragmentData<FTransformFragment>();
					TransformFragment.GetMutableTransform() = SceneComponent->GetComponentTransform();
				}
			}
			else
			{
				ensureMsgf(false, TEXT("MassAgentTransformSyncTrait only supports Actor owners."));
			}
		});

	if (EnumHasAnyFlags(SyncDirection, EMassTranslationDirection::ActorToMass))
	{
		BuildContext.AddTranslator<UTempoMassSceneComponentTransformToMassTranslator>();
	}

	if (EnumHasAnyFlags(SyncDirection, EMassTranslationDirection::MassToActor))
	{
		ensureMsgf(false, TEXT("Actor must be the authority.  Entity as the authority is not currently supported."));
	}
}
