// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoWorldSubsystem.generated.h"

// A WorldSubsystem that will only be created if it is the most-derived instance of itself.
UCLASS()
class TEMPOCORESHARED_API UTempoWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
};
