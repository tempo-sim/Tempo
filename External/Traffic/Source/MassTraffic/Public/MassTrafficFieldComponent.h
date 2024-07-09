// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"

#include "Components/PrimitiveComponent.h"
#include "MassEntityTypes.h"
#include "ZoneGraphTypes.h"

#include "MassTrafficFieldComponent.generated.h"

class UMassTrafficFieldOperationBase;
class UMassTrafficSubsystem;

UENUM(BlueprintType)
enum class EMassTrafficFieldInclusionMode : uint8
{
	// The cheapest / simplest inclusion method which includes all vehicles on lanes whose zone is overlapped by the field 
	Lanes,
	
	// Starts with Lanes inclusion and further pre-filters the vehicles on each lane by testing the transform location
	VehiclesOnLanes
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), HideCategories=(Object, HLOD, Lighting, VirtualTexture, Collision, TextureStreaming, Mobile, Physics, Tags, AssetUserData, Activation, Cooking, Navigation, Input), ShowCategories=(Mobility), meta=(BlueprintSpawnableComponent) )
class MASSTRAFFIC_API UMassTrafficFieldComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category=Field)
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category=Field)
	EMassTrafficFieldInclusionMode InclusionMode = EMassTrafficFieldInclusionMode::Lanes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Field)
	FVector Extent = FVector(50.0f);

	UPROPERTY(EditAnywhere, Category=Field)
	FZoneGraphTagFilter LaneTagFilter;

	UPROPERTY(EditAnywhere, Instanced, Category=Field, Meta = (BaseStruct = "/Script/MassTraffic.MassTrafficFieldOperationBase"))
	TArray<TObjectPtr<UMassTrafficFieldOperationBase>> Operations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Display)
	FColor Color = FColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Display)
	uint8 Alpha = 50;

	UMassTrafficFieldComponent();

	/**
	 * Execute any operation in Operations of type (or sub-class of) OperationType
	 * @param	OperationType	Type of operations to execute		
	 * @see	Operations	Base context specifying the subsystems etc operations should execute with
	 * @see UMassTrafficSubsystem::PerformFieldOperation
	 */
	void PerformFieldOperation(TSubclassOf<class UMassTrafficFieldOperationBase> OperationType, struct FMassTrafficFieldOperationContextBase& Context);

	FORCEINLINE const TArray<FZoneGraphTrafficLaneData*>& GetTrafficLanes() const 
	{
		return TrafficLanes;
	}
	
	FORCEINLINE const TArray<FMassEntityHandle>& GetTrafficIntersectionEntities() const 
	{
		return TrafficIntersectionEntities;
	}

	void UpdateOverlappedLanes(UMassTrafficSubsystem& MassTrafficSubsystem);
	void UpdateOverlappedIntersections(const UMassTrafficSubsystem& MassTrafficSubsystem);

	// UPrimitiveComponent interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	// USceneComponent interface
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

protected:
	
	void OnTrafficLaneDataChanged(UMassTrafficSubsystem* MassTrafficSubsystem);
	void OnPostInitTrafficIntersections(UMassTrafficSubsystem* MassTrafficSubsystem);

	TArray<FZoneGraphTrafficLaneData*> TrafficLanes;
	TArray<FMassEntityHandle> TrafficIntersectionEntities;
};
