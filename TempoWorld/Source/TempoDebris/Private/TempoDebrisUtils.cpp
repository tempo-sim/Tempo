// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDebrisUtils.h"

#include "Components/SplineComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#include "Kismet/KismetMathLibrary.h"

void UTempoDebrisUtils::SpawnHISMInstancesAlongSpline(USplineComponent* Spline, UHierarchicalInstancedStaticMeshComponent* HISMComponent, const FHISMSplineInstanceInfo& HISMSplineInstanceInfo)
{
	if (!Spline || !HISMComponent || !HISMSplineInstanceInfo.StaticMesh)
	{
		return;
	}

	if (FMath::IsNearlyZero(HISMSplineInstanceInfo.Spacing))
	{
		return;
	}

	HISMComponent->SetStaticMesh(HISMSplineInstanceInfo.StaticMesh);

	const float SplineLength = Spline->GetSplineLength();
	TArray<FTransform> InstanceTransforms;
	for (float SplineDistance = 0.0; SplineDistance < SplineLength; SplineDistance += HISMSplineInstanceInfo.Spacing)
	{
		for (float SplineOffset = HISMSplineInstanceInfo.Width / -2.0; SplineOffset <= HISMSplineInstanceInfo.Width / 2.0; SplineOffset += HISMSplineInstanceInfo.Spacing)
		{
			FTransform InstanceTransform;

			const FVector RightVector = Spline->GetRightVectorAtDistanceAlongSpline(SplineDistance, ESplineCoordinateSpace::World);
			const FVector UpVector = Spline->GetUpVectorAtDistanceAlongSpline(SplineDistance, ESplineCoordinateSpace::World);
			const FVector ForwardVector = FVector::CrossProduct(RightVector, UpVector);

			InstanceTransform.SetLocation(Spline->GetWorldLocationAtDistanceAlongSpline(SplineDistance) + // Location on spline
				UKismetMathLibrary::MakeRotFromYZ(RightVector, UpVector).RotateVector(HISMSplineInstanceInfo.LocationOffset) + // Nominal offset
				RightVector * SplineOffset + // Spline offset (for width)
				RightVector * FMath::RandRange(-HISMSplineInstanceInfo.RandomLocationRange.Y, HISMSplineInstanceInfo.RandomLocationRange.Y) + // Left/right random offset
				UpVector * FMath::RandRange(-HISMSplineInstanceInfo.RandomLocationRange.Z, HISMSplineInstanceInfo.RandomLocationRange.Z) + // Up/down random offset
				ForwardVector * FMath::RandRange(-HISMSplineInstanceInfo.RandomLocationRange.X, HISMSplineInstanceInfo.RandomLocationRange.X)); // Forward/back random offset

			InstanceTransform.SetRotation(FRotator(0.0, HISMSplineInstanceInfo.bRandomYaw ? FMath::RandRange(0.0, 360.0) : 0.0, 0.0).Quaternion());

			InstanceTransform.SetScale3D(FVector::OneVector + FVector(FMath::RandRange(-HISMSplineInstanceInfo.RandomScaleRange.X, HISMSplineInstanceInfo.RandomScaleRange.X),
				FMath::RandRange(-HISMSplineInstanceInfo.RandomScaleRange.Y, HISMSplineInstanceInfo.RandomScaleRange.Y),
				FMath::RandRange(-HISMSplineInstanceInfo.RandomScaleRange.Z, HISMSplineInstanceInfo.RandomScaleRange.Z)));

			InstanceTransforms.Add(InstanceTransform);
		}
	}

	HISMComponent->AddInstances(InstanceTransforms, false, true);
}
