// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

UWorldSubsystem* UTempoCoreUtils::GetSubsystemImplementingInterface(const UObject* WorldContextObject, TSubclassOf<UInterface> Interface)
{
	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		TArray<UWorldSubsystem*> Subsystems = World->GetSubsystemArray<UWorldSubsystem>();
		for (UWorldSubsystem* Subsystem : Subsystems)
		{
			if (Subsystem->GetClass()->ImplementsInterface(Interface))
			{
				return Subsystem;
			}
		}
	}
	
	return nullptr;
}
