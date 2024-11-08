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

void UMassBrightnessMeterToMassTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FMassBrightnessMeterWrapperFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEnvironmentalBrightnessFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassBrightnessMeterToMassTranslator::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
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
