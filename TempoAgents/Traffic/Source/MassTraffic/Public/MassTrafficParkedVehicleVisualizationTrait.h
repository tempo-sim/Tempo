// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#include "MassObserverProcessor.h"
#include "MassVisualizationTrait.h"

#include "MassTrafficParkedVehicleVisualizationTrait.generated.h"

UCLASS(meta=(DisplayName="Parked Vehicle Visualization"))
class MASSTRAFFIC_API UMassTrafficParkedVehicleVisualizationTrait : public UMassVisualizationTrait
{
	GENERATED_BODY()
	
public:
	
	UMassTrafficParkedVehicleVisualizationTrait();

public:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
