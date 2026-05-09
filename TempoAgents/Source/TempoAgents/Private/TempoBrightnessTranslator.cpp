// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoBrightnessTranslator.h"

#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassTrafficFragments.h"

//----------------------------------------------------------------------//
//  UMassBrightnessMeterToMassTranslator
//----------------------------------------------------------------------//
UMassBrightnessMeterToMassTranslator::UMassBrightnessMeterToMassTranslator()
	: EntityQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
	RequiredTags.Add<FMassBrightnessMeterCopyToMassTag>();
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassBrightnessMeterToMassTranslator::ConfigureQueries()
#else
void UMassBrightnessMeterToMassTranslator::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#endif
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FMassBrightnessMeterWrapperFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEnvironmentalBrightnessFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassBrightnessMeterToMassTranslator::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
#else
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
#endif
	{
		const TConstArrayView<FMassBrightnessMeterWrapperFragment> BrightnessMeterWrapperFragments = Context.GetFragmentView<FMassBrightnessMeterWrapperFragment>();
		
		const TArrayView<FEnvironmentalBrightnessFragment> EnvironmentalBrightnessFragments = Context.GetMutableFragmentView<FEnvironmentalBrightnessFragment>();
		
		const int32 NumEntities = Context.GetNumEntities();
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			if (const UTempoBrightnessMeterComponent* BrightnessMeterComponent = BrightnessMeterWrapperFragments[EntityIndex].BrightnessMeterComponent.Get())
			{
				EnvironmentalBrightnessFragments[EntityIndex].Brightness = BrightnessMeterComponent->GetBrightness();
			}
		}
	});
}
