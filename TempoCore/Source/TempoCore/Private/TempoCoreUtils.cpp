// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
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

bool UTempoCoreUtils::IsComponentDrawn(const UPrimitiveComponent* PrimitiveComponent)
{
	// IsVisible() alone is not "drawn": a component can be visible yet hidden in game (interaction
	// volumes), or drawn only into custom depth and never into the main pass (a vehicle's physics
	// skeleton, under the meshes that are actually seen).
	return PrimitiveComponent->IsVisible() && !PrimitiveComponent->bHiddenInGame && PrimitiveComponent->bRenderInMainPass;
}

FBox UTempoCoreUtils::GetActorLocalBounds(const AActor* Actor, bool bIncludeHiddenComponents)
{
	TArray<UMeshComponent*> MeshComponents;
	Actor->GetComponents<UMeshComponent>(MeshComponents);

	FBox LocalBounds(ForceInit);

	for (const UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!bIncludeHiddenComponents && !IsComponentDrawn(MeshComponent))
		{
			continue;
		}

		// Relative to the actor, scale included: the bounds are in the actor's unscaled local frame.
		auto ToActor = [Actor](const FTransform& WorldTransform)
		{
			return WorldTransform.GetRelativeTransform(Actor->GetTransform());
		};

		// A skeletal mesh's render bounds are loose by design (they must hold every pose), so its
		// physics asset's bodies, posed, give the tighter box. Anything else is bounded by its own
		// geometry: a static mesh's render bounds are its vertices' extent, where its collision can be
		// a looser hull, or absent altogether.
		bool bBoundedByBodies = false;
		if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent))
		{
			if (const UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset())
			{
				for (const USkeletalBodySetup* SkeletalBodySetup : PhysicsAsset->SkeletalBodySetups)
				{
					FBoxSphereBounds Bounds;
					SkeletalBodySetup->AggGeom.CalcBoxSphereBounds(Bounds, ToActor(SkeletalMeshComponent->GetBoneTransform(SkeletalBodySetup->BoneName)));
					LocalBounds += Bounds.GetBox();
					bBoundedByBodies = true;
				}
			}
		}
		if (!bBoundedByBodies)
		{
			// A mesh component with nothing in it (no mesh assigned, or an empty one) still reports a
			// zero-size box at its own origin, and that origin can be anywhere: CitySample's vans carry
			// empty brake-pad components whose origins swing a metre or more below the wheels.
			const FBoxSphereBounds Bounds = MeshComponent->CalcBounds(ToActor(MeshComponent->GetComponentTransform()));
			if (!Bounds.BoxExtent.IsNearlyZero())
			{
				LocalBounds += Bounds.GetBox();
			}
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
