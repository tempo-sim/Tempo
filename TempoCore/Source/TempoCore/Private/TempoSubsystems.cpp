// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSubsystems.h"

#include "TempoCoreUtils.h"

bool UTempoGameInstanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (GetClass() == UTempoGameInstanceSubsystem::StaticClass())
	{
		// Never create the base UTempoGameInstanceSubsystem
		return false;
	}

	return UTempoCoreUtils::IsMostDerivedSubclass<UTempoGameInstanceSubsystem>(GetClass());
}

bool UTempoWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (GetClass() == UTempoWorldSubsystem::StaticClass())
	{
		// Never create the base UTempoWorldSystem
		return false;
	}

	return UTempoCoreUtils::IsMostDerivedSubclass<UTempoWorldSubsystem>(GetClass());
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
