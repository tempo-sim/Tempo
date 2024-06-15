// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "MassRepresentationTypes.h"

#include "MassTrafficParkedVehicles.generated.h"

class UStaticMesh;
class UMaterialInterface;

USTRUCT()
struct MASSTRAFFIC_API FMassTrafficTypedParkingSpaces
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere)
	FName Name;

	UPROPERTY()
	TArray<FTransform> ParkingSpaces;

	UPROPERTY(Transient, VisibleAnywhere)
	int32 NumParkingSpaces = 0;
};


UCLASS(Blueprintable, BlueprintType)
class MASSTRAFFIC_API UMassTrafficParkingSpacesDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category="Parking Spaces", meta=(TitleProperty="Name"))
	TArray<FMassTrafficTypedParkingSpaces> TypedParkingSpaces;

	/** Total number of parking spaces across all TypedParkingSpaces types */
	UPROPERTY(VisibleAnywhere, Transient, Category="Parking Spaces")
	int32 NumParkingSpaces = 0;
	
#if WITH_EDITOR

	/** Populate ParkingSpaces with point transforms from ParkingSpacePointCloud where point "type" is "car" */
	UFUNCTION(CallInEditor, Category="Point Cloud")
	void PopulateParkingSpacesFromPointCloud();

	/** Clear / reset the ParkingSpaceDetails list */   
	UFUNCTION(CallInEditor, Category="Point Cloud")
	void ClearParkingSpaces()
	{
		TypedParkingSpaces.Reset();
	}

#endif

#if WITH_EDITORONLY_DATA
	
	/**
	 * RuleProcessor point cloud that contains traffic lights.
	 * IMPORTANT - This should be the same point cloud that is used to generate the city lanes, and has a particular format!
	 */
	UPROPERTY(EditAnywhere, Category="Point Cloud")
	TSoftObjectPtr<class UPointCloud> ParkingSpacesPointCloud;

	UPROPERTY(EditAnywhere, Category="Point Cloud")
	TMap<FString, FName> UnrealInstanceToParkingSpaceTypeName;

	UPROPERTY(EditAnywhere, Category="Point Cloud")
	FName DefaultParkingSpaceType;

	UPROPERTY(EditAnywhere, Category="Point Cloud")
	bool bShuffleParkingSpaces = true;

#endif

	// UObject overrides
	virtual void PostLoad() override
	{
		Super::PostLoad();

		NumParkingSpaces = 0;
		for (FMassTrafficTypedParkingSpaces& TypedParkingSpacesDesc : TypedParkingSpaces)
		{
			TypedParkingSpacesDesc.NumParkingSpaces = TypedParkingSpacesDesc.ParkingSpaces.Num();
			NumParkingSpaces += TypedParkingSpacesDesc.NumParkingSpaces;
		}
	}
};
