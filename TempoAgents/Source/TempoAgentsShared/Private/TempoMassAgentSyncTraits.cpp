// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMassAgentSyncTraits.h"

#include "MassEntityTemplateRegistry.h"
#include "MassTrafficFragments.h"
#include "TempoBrightnessTranslator.h"

//----------------------------------------------------------------------//
//  UTempoMassAgentBrightnessMeterSyncTrait
//----------------------------------------------------------------------//
void UTempoMassAgentBrightnessMeterSyncTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FMassBrightnessMeterWrapperFragment>();
	BuildContext.AddFragment<FEnvironmentalBrightnessFragment>();

	BuildContext.GetMutableObjectFragmentInitializers().Add([=](UObject& Owner, FMassEntityView& EntityView, const EMassTranslationDirection CurrentDirection)
		{
			AActor* OwnerActor = Cast<AActor>(&Owner);
			if (!ensureMsgf(OwnerActor != nullptr, TEXT("Owner must be an Actor.")))
			{
				return;
			}

			// Note:  When Mass considers its Actor representations to be puppets,
			// it only initializes them with CurrentDirection == EMassTranslationDirection::MassToActor.
			// However, in this case, Mass needs information from the Actor in order to make decisions.
			// For example, Mass needs to know about the environmental brightness in order to control
			// the vehicles' headlights.

			UTempoBrightnessMeter* BrightnessMeterComponent = OwnerActor->FindComponentByClass<UTempoBrightnessMeter>();
			if (!ensureMsgf(BrightnessMeterComponent != nullptr, TEXT("UTempoMassAgentBrightnessMeterSyncTrait expects OwnerActor to have a UTempoBrightnessMeter component.")))
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