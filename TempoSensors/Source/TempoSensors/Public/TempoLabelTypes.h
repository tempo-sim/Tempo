// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"

#include "TempoLabelTypes.generated.h"

class UNiagaraSystem;

// The row naming the label ID worn by everything the table does not otherwise match. Objects that
// resolve to no row are labeled 0, so this row's Label must be 0 for the table to describe what
// the label image actually contains; ValidateSemanticLabelTable enforces both.
inline constexpr const TCHAR* GNoLabelRowName = TEXT("NoLabel");

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

	// The Actor tags which should be tagged with this label (overrides labels at the actor type level).
	// The escape hatch for Actors their class can't distinguish: one instance of a class labeled
	// differently from the rest, or an Actor assembled at runtime whose class says nothing useful.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<FName> ActorTags;

	// The StaticMesh types which should be tagged with this label (overrides labels at the actor level).
	// Also matches the meshes a Niagara mesh renderer instances.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<TSoftObjectPtr<UStaticMesh>> StaticMeshTypes;

	// The SkeletalMesh types which should be tagged with this label (overrides labels at the actor level).
	// Separate from StaticMeshTypes rather than one widened column, because the two asset types share
	// no base narrower than UStreamableRenderAsset (which textures also derive from) and the existing
	// static-mesh RPCs name their type on the wire.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<TSoftObjectPtr<USkeletalMesh>> SkeletalMeshTypes;

	// The Niagara systems whose components should be tagged with this label (overrides labels at the
	// actor level, alongside the mesh columns). Labels an effect by what it is, exhaust or dust, on
	// every actor that plays it; a sprite or ribbon emitter has no mesh to match otherwise, and the
	// lidar labels the medium such an effect produces with this. A system with mesh renderers whose
	// meshes are also labeled resolves to the system's row.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<TSoftObjectPtr<UNiagaraSystem>> NiagaraSystemTypes;

	// The component tags which should be tagged with this label (overrides labels at the mesh and actor levels).
	// The escape hatch for components no mesh asset can identify: components whose mesh is built at
	// runtime, components with no mesh at all, or one instance of a mesh labeled differently elsewhere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<FName> ComponentTags;
};
