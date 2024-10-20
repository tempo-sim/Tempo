// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "MassAgentTraits.h"
#include "MassEntityTraitBase.h"

#include "TempoMassAgentSyncTraits.generated.h"

UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Agent Brightness Meter Sync"))
class TEMPOAGENTSSHARED_API UTempoMassAgentBrightnessMeterSyncTrait : public UMassAgentSyncTrait
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
