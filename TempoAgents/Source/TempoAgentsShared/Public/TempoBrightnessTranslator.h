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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
