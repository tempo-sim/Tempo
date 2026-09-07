// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

UWorldSubsystem* UTempoCoreUtils::GetSubsystemImplementingInterface(const UObject* WorldContextObject, TSubclassOf<UInterface> Interface)
{
	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UWorldSubsystem* SubsystemImplementingInterface = nullptr;
		World->ForEachSubsystem<UWorldSubsystem>([&Interface, &SubsystemImplementingInterface](UWorldSubsystem* Subsystem)
		{
			if (Subsystem->GetClass()->ImplementsInterface(Interface))
			{
				SubsystemImplementingInterface = Subsystem;
			}
		});
		return SubsystemImplementingInterface;
	}

	return nullptr;
}

bool UTempoCoreUtils::IsGameWorld(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject->GetWorld();
	return World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE;
}

FBox UTempoCoreUtils::GetActorLocalBounds(const AActor* Actor, bool bIncludeHiddenComponents)
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	FBox LocalBounds;

	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent->IsVisible() && !bIncludeHiddenComponents)
		{
			continue;
		}

		auto AddAggGeomToBounds = [Actor, &LocalBounds](const FKAggregateGeom& AggGeom, const FTransform& WorldTransform)
		{
			FBoxSphereBounds Bounds;
			const FTransform RelativeTransform = WorldTransform.GetRelativeTransform(Actor->GetTransform());
			AggGeom.CalcBoxSphereBounds(Bounds, RelativeTransform);
			LocalBounds += Bounds.GetBox();
		};

		if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimitiveComponent))
		{
			if (const UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset())
			{
				for (const USkeletalBodySetup* SkeletalBodySetup : PhysicsAsset->SkeletalBodySetups)
				{
					AddAggGeomToBounds(SkeletalBodySetup->AggGeom, SkeletalMeshComponent->GetBoneTransform(SkeletalBodySetup->BoneName));
				}
			}
		}
		else if(const UBodySetup* BodySetup = PrimitiveComponent->BodyInstance.GetBodySetup())
		{
			AddAggGeomToBounds(BodySetup->AggGeom, PrimitiveComponent->GetComponentTransform());
		}
	}

	return LocalBounds;
}

FString UTempoCoreUtils::GetActorIdentifier(const AActor* Actor)
{
#if WITH_EDITOR
	// Materialize the actor label now so it matches every later GetActorNameOrLabel() call,
	// including GetActorWithName lookups. See header for details.
	(void)Actor->GetActorLabel();
#endif
	return Actor->GetActorNameOrLabel();
}

FString UTempoCoreUtils::StripBlueprintClassSuffix(const FString& ClassName)
{
	return ClassName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive) ? ClassName.LeftChop(2) : ClassName;
}
