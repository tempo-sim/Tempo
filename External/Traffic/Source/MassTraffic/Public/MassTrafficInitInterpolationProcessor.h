// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassEntityQuery.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficFragments.h"
#include "MassCommonFragments.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficInitInterpolationProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficInitInterpolationProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficInitInterpolationProcessor();
	
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
