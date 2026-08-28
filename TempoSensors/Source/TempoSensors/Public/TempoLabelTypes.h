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
	// Also matches the meshes a Niagara mesh renderer instances.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<TSoftObjectPtr<UStaticMesh>> StaticMeshTypes;

	// The component tags which should be tagged with this label (overrides labels at the mesh and actor levels).
	// The escape hatch for components no mesh asset can identify: components whose mesh is built at
	// runtime, components with no mesh at all, or one instance of a mesh labeled differently elsewhere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<FName> ComponentTags;
};
