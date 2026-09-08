// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassObserverProcessor.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassVisualizationTrait.h"
#include "MassTrafficVehicleVisualizationTrait.generated.h"


UCLASS(meta=(DisplayName="Traffic Vehicle Visualization"))
class MASSTRAFFIC_API UMassTrafficVehicleVisualizationTrait : public UMassVisualizationTrait
{
	GENERATED_BODY()
	
public:
	UMassTrafficVehicleVisualizationTrait();
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
