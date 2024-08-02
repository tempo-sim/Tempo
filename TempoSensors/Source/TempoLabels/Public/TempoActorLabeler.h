// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoActorLabeler.generated.h"

/**
 * Tags all meshes on all Actors in the world with the appropriate label.
 */
UCLASS()
class TEMPOLABELS_API UTempoActorLabeler : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	void OnWorldPreActorTick(UWorld* World, ELevelTick TickType, float DeltaTime);
	
	void BuildLabelMaps();
	
	void LabelAllActors();
	
	void LabelActor(AActor* Actor);

	void LabelComponents(const AActor* Actor, int32 ActorLabelId);

	UPROPERTY(VisibleAnywhere)
	UDataTable* SemanticLabelTable;

	UPROPERTY(VisibleAnywhere)
	TMap<TSubclassOf<AActor>, FName> ActorLabels;

	UPROPERTY(VisibleAnywhere)
	TMap<FString, FName> StaticMeshLabels;

	UPROPERTY(VisibleAnywhere)
	TMap<FName, int32> LabelIds;

	// Cache to avoid re-labeling the same Actors over and over
	UPROPERTY()
	TMap<AActor*, int32> LabeledActors;

	// Cache to avoid re-labeling the same Components over and over
	UPROPERTY()
	TMap<UPrimitiveComponent*, const UStaticMesh*> LabeledComponents;
};
