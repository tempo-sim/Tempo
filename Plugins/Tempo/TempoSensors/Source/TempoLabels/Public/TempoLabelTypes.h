// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "TempoLabelTypes.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct FSemanticLabel: public FTableRowBase
{
	GENERATED_BODY()

	// The raw label that will be stored in the stencil buffer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Label = 0;

	// The Actor types which should be tagged with this label.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<TSubclassOf<AActor>> ActorTypes;

	// The StaticMesh types which should be tagged with this label (overrides labels at the actor level).
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<TSoftObjectPtr<UStaticMesh>> StaticMeshTypes;
};
