// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassObserverProcessor.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassVisualizationTrait.h"
#include "MassTrafficTrailerVisualizationTrait.generated.h"

UCLASS(meta=(DisplayName="Trailer Visualization"))
class MASSTRAFFIC_API UMassTrafficTrailerVisualizationTrait : public UMassVisualizationTrait
{
	GENERATED_BODY()
	
public:
	
	UMassTrafficTrailerVisualizationTrait();

public:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
