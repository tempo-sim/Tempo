// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoSubsystems.generated.h"

// A GameInstanceSubsystem that will only be created if it is the most-derived instance of itself.
UCLASS(Abstract)
class TEMPOCORESHARED_API UTempoGameInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
};


// A WorldSubsystem that will only be created if it is the most-derived instance of itself.
UCLASS(Abstract)
class TEMPOCORESHARED_API UTempoWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
};

// A TempoWorldSubsystem that will only be created for game worlds.
UCLASS(Abstract)
class TEMPOCORESHARED_API UTempoGameWorldSubsystem : public UTempoWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
};

// A TickableWorldSubsystem that will only be created if it is the most-derived instance of itself.
UCLASS(Abstract)
class TEMPOCORESHARED_API UTempoTickableWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
};

// A TempoTickableWorldSubsystem that will only be created for game worlds.
UCLASS(Abstract)
class TEMPOCORESHARED_API UTempoTickableGameWorldSubsystem : public UTempoTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
};
