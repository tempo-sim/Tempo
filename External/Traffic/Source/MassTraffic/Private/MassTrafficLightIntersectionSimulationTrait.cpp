// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficLightIntersectionSimulationTrait.h"
#include "MassTrafficFragments.h"

#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"

void UMassTrafficLightIntersectionSimulationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddTag<FMassTrafficIntersectionTag>();
	BuildContext.RequireFragment<FMassTrafficLightIntersectionFragment>();
	BuildContext.RequireFragment<FTransformFragment>();
}
