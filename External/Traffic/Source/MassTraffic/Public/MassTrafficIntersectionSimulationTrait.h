// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"

#include "MassTrafficIntersectionSimulationTrait.generated.h"

UCLASS(meta=(DisplayName="Traffic Intersection Simulation"))
class MASSTRAFFIC_API UMassTrafficIntersectionSimulationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
