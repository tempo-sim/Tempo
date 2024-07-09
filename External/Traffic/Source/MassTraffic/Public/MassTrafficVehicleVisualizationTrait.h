// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#include "MassObserverProcessor.h"
#include "MassVisualizationTrait.h"

#include "MassTrafficVehicleVisualizationTrait.generated.h"

UCLASS(meta=(DisplayName="Traffic Vehicle Visualization"))
class MASSTRAFFIC_API UMassTrafficVehicleVisualizationTrait : public UMassVisualizationTrait
{
	GENERATED_BODY()
	
public:
	
	UMassTrafficVehicleVisualizationTrait();

public:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
