// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "PhysicsEngine/BodySetup.h"

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

bool UTempoCoreUtils::IsGameWorld(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject->GetWorld();
	return World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE;
}

FBox UTempoCoreUtils::GetActorLocalBounds(const AActor* Actor)
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	FBox LocalBounds;

	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (const UBodySetup* BodySetup = PrimitiveComponent->BodyInstance.GetBodySetup())
		{
			FBoxSphereBounds Bounds;
			FTransform RelativeTransform = Actor->GetTransform().GetRelativeTransform(PrimitiveComponent->GetComponentTransform());
			BodySetup->AggGeom.CalcBoxSphereBounds(Bounds, RelativeTransform.Inverse());
			LocalBounds += Bounds.GetBox();
		}
	}

	return LocalBounds;
}
