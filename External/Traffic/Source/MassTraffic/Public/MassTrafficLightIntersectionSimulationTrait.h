// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"

#include "MassTrafficLightIntersectionSimulationTrait.generated.h"

UCLASS(meta=(DisplayName="Traffic Light Intersection Simulation"))
class MASSTRAFFIC_API UMassTrafficLightIntersectionSimulationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
