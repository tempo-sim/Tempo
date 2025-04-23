// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficSignIntersectionSimulationTrait.h"
#include "MassTrafficFragments.h"

#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"

void UMassTrafficSignIntersectionSimulationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddTag<FMassTrafficIntersectionTag>();
	BuildContext.RequireFragment<FMassTrafficSignIntersectionFragment>();
	BuildContext.RequireFragment<FTransformFragment>();
}
