// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"

#include "MassTrafficSignIntersectionSimulationTrait.generated.h"

UCLASS(meta=(DisplayName="Traffic Sign Intersection Simulation"))
class MASSTRAFFIC_API UMassTrafficSignIntersectionSimulationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
