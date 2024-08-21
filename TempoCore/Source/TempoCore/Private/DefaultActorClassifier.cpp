// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "DefaultActorClassifier.h"

#include "GameFramework/Character.h"
#include "WheeledVehiclePawn.h"

// Some very crude logic to classify Actors in the absence of a more detailed classification scheme.
FName UDefaultActorClassifier::GetDefaultActorClassification(const AActor* Actor)
{
	if (Actor->IsA(AWheeledVehiclePawn::StaticClass()))
	{
		return FName(TEXT("Vehicle"));
	}

	if (Actor->IsA(ACharacter::StaticClass()))
	{
		return FName(TEXT("Person"));
	}

	switch (Actor->GetRootComponent()->GetCollisionObjectType())
	{
	case ECC_WorldStatic:
		{
			return FName(TEXT("Other_WorldStatic"));
		}
	case ECC_WorldDynamic:
		{
			return FName(TEXT("Other_WorldDynamic"));
		}
	case ECC_Pawn:
		{
			return FName(TEXT("Other_Pawn"));
		}
	case ECC_PhysicsBody:
		{
			return FName(TEXT("Other_PhysicsBody"));
		}
	case ECC_Vehicle:
		{
			return FName(TEXT("Vehicle"));
		}
	case ECC_Destructible:
		{
			return FName(TEXT("Other_Destructible"));
		}
	default:
		{
			break;
		}
	}

	return NAME_None;
}

FName UDefaultActorClassifier::GetActorClassification(const AActor* Actor) const
{
	return GetDefaultActorClassification(Actor);
}
