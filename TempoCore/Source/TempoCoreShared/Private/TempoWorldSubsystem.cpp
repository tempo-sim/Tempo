// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSubsystem.h"

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
