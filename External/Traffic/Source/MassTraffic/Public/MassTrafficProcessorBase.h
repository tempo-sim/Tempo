// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficSubsystem.h"
#include "MassProcessor.h"
#include "MassTrafficProcessorBase.generated.h"

/**
 * Base class for traffic processors that caches a pointer to the traffic subsytem
 */
UCLASS(Abstract)
class MASSTRAFFIC_API UMassTrafficProcessorBase : public UMassProcessor
{
	GENERATED_BODY()

public:
	
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void Initialize(UObject& InOwner) override;
#else
	virtual void InitializeInternal(UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	
protected:

	TWeakObjectPtr<const UMassTrafficSettings> MassTrafficSettings;

	FRandomStream RandomStream;

	UPROPERTY(transient)
	UObject* LogOwner;
};
