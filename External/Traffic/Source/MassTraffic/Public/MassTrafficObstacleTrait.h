// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"

#include "MassTrafficObstacleTrait.generated.h"

UCLASS(meta=(DisplayName="Traffic Obstacle"))
class MASSTRAFFIC_API UMassTrafficObstacleTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
