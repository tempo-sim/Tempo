// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#include "MassObserverProcessor.h"
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
