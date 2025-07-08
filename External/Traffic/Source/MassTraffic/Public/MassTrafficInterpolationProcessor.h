// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassTrafficInterpolationProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficInterpolationProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficInterpolationProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQueryNonOffLOD_Conditional;
	FMassEntityQuery EntityQueryOffLOD_Conditional;
};
