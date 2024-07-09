// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficDamage.h"

#include "MassRepresentationProcessor.h"
#include "MassActorSubsystem.h"
#include "MassTrafficInstancePlaybackHelpers.h"

#include "MassTrafficDriverVisualizationProcessor.generated.h"

class UAnimToTextureDataAsset;
class UMassTrafficSubsystem;

/**
 * Overridden visualization processor to make it tied to the TrafficVehicle via the requirements
 */
UCLASS(HideCategories=("Mass|LOD"))
class MASSTRAFFIC_API UMassTrafficDriverVisualizationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficDriverVisualizationProcessor();

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:

	bool PopulateAnimEvalFromAnimState(
		const UAnimToTextureDataAsset* AnimData,
		int32 StateIndex,
		int32 VariationIndex,
		float EvalInput,
		const FFloatInterval& InputInterval,
		FMassTrafficInstancePlaybackData& OutPlaybackData);

	bool PopulateAnimPlaybackFromAnimState(
		const UAnimToTextureDataAsset* AnimData, 
		int32 StateIndex, 
		int32 VariationIndex, 
		float GlobalStartTime,
		FMassTrafficInstancePlaybackData& OutPlaybackData);

	bool PopulateAnimFromAnimState(
		const UAnimToTextureDataAsset* AnimData, 
		int32 StateIndex, 
		int32 VariationIndex,
		FMassTrafficInstancePlaybackData& OutPlaybackData);

public:

	UPROPERTY(EditDefaultsOnly, config)
	float PlaybackSteeringThreshold = 0.05f;

	UPROPERTY(EditDefaultsOnly, config)
	float LowSpeedThreshold = 10.0f;

	UPROPERTY(EditDefaultsOnly, config)
	float LookIdleMinDistSqrd = 250000.0f;

	UPROPERTY(EditDefaultsOnly, config)
	float LookIdleMinDotToPlayer = 0.5f;

	UPROPERTY(EditDefaultsOnly, config)
	float AlternateDrivingStanceRatio = 0.5f;

	UPROPERTY(EditDefaultsOnly, config)
	EMassTrafficVehicleDamageState RemoveDriverDamageThreshold = EMassTrafficVehicleDamageState::Totaled;

	/** Caching ptr to our associated world */
	UPROPERTY(Transient)
	UWorld* World;

	FMassEntityQuery EntityQuery_Conditional;
};