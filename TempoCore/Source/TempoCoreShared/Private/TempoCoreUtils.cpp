// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 4
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

UWorldSubsystem* UTempoCoreUtils::GetSubsystemImplementingInterface(const UObject* WorldContextObject, TSubclassOf<UInterface> Interface)
{
	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UWorldSubsystem* SubsystemImplementingInterface = nullptr;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
		TArray<UWorldSubsystem*> Subsystems = World->GetSubsystemArray<UWorldSubsystem>();
		for (UWorldSubsystem* Subsystem : Subsystems)
		{
			if (Subsystem->GetClass()->ImplementsInterface(Interface))
			{
				SubsystemImplementingInterface = Subsystem;
			}
		}
#else
		World->ForEachSubsystem<UWorldSubsystem>([&Interface, &SubsystemImplementingInterface](UWorldSubsystem* Subsystem)
		{
			if (Subsystem->GetClass()->ImplementsInterface(Interface))
			{
				SubsystemImplementingInterface = Subsystem;
			}
		});
#endif
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

FPlane UTempoCoreUtils::BestFitPlaneFromFourPoints(const FVector& Point1, const FVector& Point2, const FVector& Point3, const FVector& Point4)
{
	const TStaticArray<FVector,4>& Points { Point1, Point2, Point3, Point4 };

	// Compute the normals from every combination of three points, by looking to the left and right of each corner.
	TArray<FVector> AllNormals;
	for (int32 I = 0; I < 4; ++I)
	{
		int32 J = I == 3 ? 0 : I + 1; // (I + 1) % 4
		int32 K = I == 0 ? 3 : I - 1; // (I - 1) % 4
		const FVector& PointI = Points[I];
		const FVector& PointJ = Points[J];
		const FVector& PointK = Points[K];
		const FVector IJ = PointJ - PointI;
		const FVector IK = PointK - PointI;
		AllNormals.Add(FVector::CrossProduct(IK, IJ).GetSafeNormal());
	}

	FVector NormalAvgNumerator = FVector::ZeroVector;
	for (const FVector& Normal : AllNormals)
	{
		NormalAvgNumerator += Normal;
	}

	FVector PointAvgNumerator = FVector::ZeroVector;
	for (const FVector& Point : Points)
	{
		PointAvgNumerator += Point;
	}

	const FVector AvgNormal = NormalAvgNumerator / 4.0;
	const FVector AvgPoint = PointAvgNumerator / 4.0;

	return FPlane(AvgPoint, AvgNormal);
}
