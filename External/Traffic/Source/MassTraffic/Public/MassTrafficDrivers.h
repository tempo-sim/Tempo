// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassLODSubsystem.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntityTypes.h"
#include "MassRepresentationTypes.h"
#include "Engine/DataTable.h"
#include "AnimToTextureDataAsset.h"
#include "MassTrafficDrivers.generated.h"

class UStaticMesh;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficDriverMesh
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UStaticMesh* StaticMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;

	/** The minimum inclusive LOD significance to start using this static mesh */
	UPROPERTY(EditAnywhere)
	float MinLODSignificance = static_cast<float>(EMassLOD::High);

	/** The maximum exclusive LOD significance to stop using this static mesh */
	UPROPERTY(EditAnywhere)
	float MaxLODSignificance = static_cast<float>(EMassLOD::Max);
};

UENUM(BlueprintType)
enum class EDriverAnimStateVariation : uint8
{
	TwoHands = 0,
	
	OneHand = 1,
	
	Bus = 2,
	
	None = 3
};

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficDriverTypeData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Name;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FMassTrafficDriverMesh> Meshes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<const UAnimToTextureDataAsset> AnimationData = nullptr;
};

UCLASS(Blueprintable, BlueprintType)
class MASSTRAFFIC_API UMassTrafficDriverTypesDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(TitleProperty="Name"))
	TArray<FMassTrafficDriverTypeData> DriverTypes;
};

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficDriversParameters : public FMassConstSharedFragment
{
	GENERATED_BODY()
	
	/** External driver types to use in addition to Config.DataTypes */
	UPROPERTY(EditAnywhere, Category = "Mass Traffic|Drivers")
	TObjectPtr<const UMassTrafficDriverTypesDataAsset> DriverTypesData = nullptr;
	
	// Offset transform applied relative to the vehicle world transform to position drivers into the car
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform DriversSeatOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay)
	EDriverAnimStateVariation AnimStateVariationOverride = EDriverAnimStateVariation::None;

	UPROPERTY(Transient)
	TArray<FStaticMeshInstanceVisualizationDescHandle> DriverTypesStaticMeshDescHandle;
};
