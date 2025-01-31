// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDebrisUtils.h"

#include "Components/SplineComponent.h"
#include "InstancedFoliageActor.h"

void UTempoDebrisUtils::SpawnFoliageAlongSpline(UObject* WorldContextObject, USplineComponent* Spline, const TArray<FSplineFoliageSpawnInfo>& FoliageSpawnInfos)
{
	UWorld* World = WorldContextObject->GetWorld();

	if (!(World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor))
	{
		return;
	}

	AInstancedFoliageActor* FoliageActor = World->GetCurrentLevel()->InstancedFoliageActor.Get();

	if (!FoliageActor)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.OverrideLevel = World->GetCurrentLevel();
		FoliageActor = World->SpawnActor<AInstancedFoliageActor>(SpawnParams);
		World->GetCurrentLevel()->InstancedFoliageActor = FoliageActor;
	}
	
	for (const FSplineFoliageSpawnInfo& SplineFoliageSpawnInfo : FoliageSpawnInfos)
	{
		if (!SplineFoliageSpawnInfo.FoliageType || SplineFoliageSpawnInfo.Spacing == 0.0)
		{
			continue;
		}

		if (FFoliageInfo* FoliageInfo = FoliageActor->FindOrAddMesh(SplineFoliageSpawnInfo.FoliageType))
		{
			const float SplineLength = Spline->GetSplineLength();
			TArray<const FFoliageInstance*> FoliageInstances;
			for (float SplineDistance = 0.0; SplineDistance < SplineLength; SplineDistance += SplineFoliageSpawnInfo.Spacing)
			{
				for (float SplineOffset = SplineFoliageSpawnInfo.Width / -2.0; SplineOffset < UE_SMALL_NUMBER + SplineFoliageSpawnInfo.Width / 2.0; SplineOffset += SplineFoliageSpawnInfo.Spacing)
				{
					FFoliageInstance* FoliageInstance = new FFoliageInstance();
					const FVector RightVector = Spline->GetRightVectorAtDistanceAlongSpline(SplineDistance, ESplineCoordinateSpace::World);
					FoliageInstance->Location = Spline->GetWorldLocationAtDistanceAlongSpline(SplineDistance);
					FoliageInstance->Location += (SplineFoliageSpawnInfo.OffsetRight + SplineOffset) * RightVector;
					FoliageInstance->Location += SplineFoliageSpawnInfo.OffsetUp * FVector::UpVector;
					FoliageInstance->Location += FMath::RandRange(-SplineFoliageSpawnInfo.RandLocationRangeLeftRight, SplineFoliageSpawnInfo.RandLocationRangeLeftRight) * FVector::RightVector;
					FoliageInstance->Location += FMath::RandRange(-SplineFoliageSpawnInfo.RandLocationRangeForwardBack, SplineFoliageSpawnInfo.RandLocationRangeForwardBack) * FVector::ForwardVector;
					FoliageInstance->DrawScale3D += FMath::RandRange(-SplineFoliageSpawnInfo.RandScaleFactorRange, SplineFoliageSpawnInfo.RandScaleFactorRange) * FVector3f::OneVector;
					FoliageInstance->Rotation.Yaw = FMath::RandRange(0.0, 360.0);
					FoliageInstances.Add(FoliageInstance);
				}
			}
			
			FoliageInfo->AddInstances(SplineFoliageSpawnInfo.FoliageType, FoliageInstances);
			FoliageInstances.Empty();
		}
	}
}
