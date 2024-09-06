// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSubsystem.h"

#include "TempoCoreUtils.h"

bool UTempoWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (GetClass() == UTempoWorldSubsystem::StaticClass())
	{
		// Never create the base UTempoWorldSystem
		return false;
	}

	// RF_NoFlags to include CDO
	for (TObjectIterator<UTempoWorldSubsystem> WorldSubsystemIt(EObjectFlags::RF_NoFlags); WorldSubsystemIt; ++WorldSubsystemIt)
	{
		const UTempoWorldSubsystem* WorldSubsystem = *WorldSubsystemIt;
		if (WorldSubsystem->GetClass() != GetClass() && WorldSubsystem->IsA(GetClass()))
		{
			// There is a more derived version of ourselves
			return false;
		}
	}

	return true;
}

bool UTempoGameWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!UTempoCoreUtils::IsGameWorld(Outer))
	{
		return false;
	}
	return Super::ShouldCreateSubsystem(Outer);
}

bool UTempoTickableWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (GetClass() == UTempoTickableWorldSubsystem::StaticClass())
	{
		// Never create the base UTempoTickableWorldSubsystem
		return false;
	}

	// RF_NoFlags to include CDO
	for (TObjectIterator<UTempoTickableWorldSubsystem> WorldSubsystemIt(EObjectFlags::RF_NoFlags); WorldSubsystemIt; ++WorldSubsystemIt)
	{
		const UTempoTickableWorldSubsystem* WorldSubsystem = *WorldSubsystemIt;
		if (WorldSubsystem->GetClass() != GetClass() && WorldSubsystem->IsA(GetClass()))
		{
			// There is a more derived version of ourselves
			return false;
		}
	}

	return true;
}

bool UTempoTickableGameWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!UTempoCoreUtils::IsGameWorld(Outer))
	{
		return false;
	}
	return Super::ShouldCreateSubsystem(Outer);
}
