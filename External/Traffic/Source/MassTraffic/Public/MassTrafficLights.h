// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "MassLODSubsystem.h"
#include "MassRepresentationTypes.h"

#include "MassTrafficLights.generated.h"

class UStaticMesh;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficLightTypeData
{
	GENERATED_BODY()

	FMassTrafficLightTypeData() = default;
	
	FMassTrafficLightTypeData(const FName& InName, const FStaticMeshInstanceVisualizationDesc& InStaticMeshInstanceDesc, const int32 InNumLanes) :
		Name(InName),
		StaticMeshInstanceDesc(InStaticMeshInstanceDesc),
		NumLanes(InNumLanes)
	{
	}
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Name;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FStaticMeshInstanceVisualizationDesc StaticMeshInstanceDesc;

	/** This light is suitable for roads with this many lanes. 0 = Any */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumLanes = 0;

	bool operator==(const FMassTrafficLightTypeData& Other) const
	{
		return Name == Other.Name && StaticMeshInstanceDesc == Other.StaticMeshInstanceDesc && NumLanes == Other.NumLanes;
	}
};

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficLightInstanceDesc
{
	GENERATED_BODY()
	FMassTrafficLightInstanceDesc()
	{}

	FMassTrafficLightInstanceDesc(const FVector& InPosition, const float InZRotation, const FVector& InControlledIntersectionMidpoint, const uint8 InTrafficLightTypeIndex, const FVector& InMeshScale) :
		Position(InPosition),
		ZRotation(InZRotation),
		ControlledIntersectionSideMidpoint(InControlledIntersectionMidpoint),
		TrafficLightTypeIndex(InTrafficLightTypeIndex),
		MeshScale(InMeshScale)
	{
	}

	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	UPROPERTY()
	float ZRotation = 0.0f;

	UPROPERTY()
	FVector ControlledIntersectionSideMidpoint = FVector::ZeroVector;

	UPROPERTY()
	int16 TrafficLightTypeIndex = INDEX_NONE;

	UPROPERTY()
	FVector MeshScale = FVector::OneVector;
};

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficLightsParameters : public FMassConstSharedFragment
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FMassTrafficLightTypeData> TrafficLightTypes;
	
	UPROPERTY(Transient)
	TArray<FStaticMeshInstanceVisualizationDescHandle> TrafficLightTypesStaticMeshDescHandle;
};
