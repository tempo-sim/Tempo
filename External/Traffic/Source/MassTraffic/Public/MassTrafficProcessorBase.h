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
	
	virtual void Initialize(UObject& InOwner) override;
	
protected:

	TWeakObjectPtr<const UMassTrafficSettings> MassTrafficSettings;

	FRandomStream RandomStream;

	UPROPERTY(transient)
	UObject* LogOwner;
};
