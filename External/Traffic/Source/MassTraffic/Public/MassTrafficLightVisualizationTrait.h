// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassObserverProcessor.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficLights.h"
#include "MassVisualizationTrait.h"
#include "MassTrafficLightVisualizationTrait.generated.h"


UCLASS(meta=(DisplayName="Traffic Light Visualization"))
class MASSTRAFFIC_API UMassTrafficLightVisualizationTrait : public UMassVisualizationTrait
{
	GENERATED_BODY()
	
public:
	
	UMassTrafficLightVisualizationTrait();

public:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual void SanitizeParams(FMassRepresentationParameters& InOutParams, const bool bStaticMeshDeterminedInvalid = false) const override;

#if WITH_EDITOR
	virtual bool ValidateParams() const override;
#endif // WITH_EDITOR
};
