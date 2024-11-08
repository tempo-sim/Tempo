// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoBrightnessMeterComponent.h"

#include "MassCommonFragments.h"
#include "MassTranslator.h"

#include "TempoBrightnessTranslator.generated.h"


USTRUCT()
struct FMassBrightnessMeterWrapperFragment : public FMassFragment
{
	GENERATED_BODY()
	
	TWeakObjectPtr<UTempoBrightnessMeterComponent> BrightnessMeterComponent;
};

USTRUCT()
struct FMassBrightnessMeterCopyToMassTag : public FMassTag
{
	GENERATED_BODY()
};

UCLASS()
class TEMPOAGENTSSHARED_API UMassBrightnessMeterToMassTranslator : public UMassTranslator
{
	GENERATED_BODY()

public:
	UMassBrightnessMeterToMassTranslator();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
