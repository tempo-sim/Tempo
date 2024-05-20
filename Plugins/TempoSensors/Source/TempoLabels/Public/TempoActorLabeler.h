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

private:
	void LabelAllActors() const;
	
	void LabelActor(AActor* Actor) const;

	UPROPERTY(VisibleAnywhere)
	UDataTable* SemanticLabelTable;
};
