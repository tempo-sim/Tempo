// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "ActorClassificationInterface.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoSensorsConstants.h"

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

#include "TempoActorLabeler.generated.h"

// Hands out the IDs in [MinId, MaxId], always the one with the fewest live allocations, so IDs are
// only shared between live objects once every ID is held, and then as evenly as possible. Knows
// nothing of the instance label uniqueness settings; the labeler applies those.
struct FInstanceIdAllocator
{
	FInstanceIdAllocator() = default;
	FInstanceIdAllocator(int32 MinIdIn, int32 MaxIdIn);

	// The ID with the fewest live allocations. Unset when every ID is already held and
	// bAllowSharedIds is false, or the range is empty.
	TOptional<int32> Allocate(bool bAllowSharedIds);

	// Gives back one allocation of Id. Returning an ID with no live allocation is a caller bug,
	// and is reported rather than counted.
	void Return(int32 Id);

private:
	int32 MinId = 0;
	int32 MaxId = -1;
	// IdsByLiveCount[N] is the set of IDs with exactly N live allocations. Every ID in
	// [MinId, MaxId] is in exactly one of these sets at all times: Allocate moves it from its set
	// to the next one up, Return from its set to the next one down. Index 0 always exists; higher
	// sets are added as IDs come to be shared and trimmed once no ID is shared that many times.
	TArray<TSet<int32>> IdsByLiveCount;
};

USTRUCT()
struct FInstanceSemanticIdPair
{
	GENERATED_BODY()

	int32 InstanceId = 0;
	int32 SemanticId = 0;
};

namespace TempoCore
{
	class Empty;
}

namespace TempoSensors
{
	class InstanceToSemanticIdMap;
	class GetAllActorLabelsResponse;
	class GetLabeledActorTypesResponse;
	class GetSemanticClassesResponse;
	class SetActorTypeSemanticIdRequest;
	class GetAllStaticMeshTypesResponse;
	class SetStaticMeshTypeSemanticIdRequest;
	class GetAllSkeletalMeshTypesResponse;
	class SetSkeletalMeshTypeSemanticIdRequest;
	class SetActorTagSemanticIdRequest;
	class GetLabelTableAsJsonResponse;
	class SetLabelTypeRequest;
	class LoadLabelTableRequest;
	class SetInstanceLabelUniquenessRequest;
	class SetLabelRowOverridesRequest;
}

/**
 * Tags all meshes on all Actors in the world with the appropriate label.
 */
UCLASS()
class TEMPOSENSORS_API UTempoActorLabeler : public UTempoGameWorldSubsystem, public IActorClassificationInterface, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	virtual FName GetActorClassification(const AActor* Actor) const override;

	virtual void RegisterServices(FTempoServer& Server) override;

	void GetInstanceToSemanticIdMap(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::InstanceToSemanticIdMap>& ResponseContinuation) const;

	void HandleGetAllActorLabels(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetAllActorLabelsResponse>& ResponseContinuation);

	void HandleGetLabeledActorTypes(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetLabeledActorTypesResponse>& ResponseContinuation);

	void HandleGetSemanticClasses(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetSemanticClassesResponse>& ResponseContinuation);

	void HandleSetActorTypeSemanticId(const TempoSensors::SetActorTypeSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void HandleGetAllStaticMeshTypes(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetAllStaticMeshTypesResponse>& ResponseContinuation);

	void HandleSetStaticMeshTypeSemanticId(const TempoSensors::SetStaticMeshTypeSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void HandleGetAllSkeletalMeshTypes(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetAllSkeletalMeshTypesResponse>& ResponseContinuation);

	void HandleSetSkeletalMeshTypeSemanticId(const TempoSensors::SetSkeletalMeshTypeSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void HandleSetActorTagSemanticId(const TempoSensors::SetActorTagSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void HandleGetLabelTableAsJson(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetLabelTableAsJsonResponse>& ResponseContinuation) const;

	void HandleSetLabelType(const TempoSensors::SetLabelTypeRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void HandleLoadLabelTable(const TempoSensors::LoadLabelTableRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void HandleSetInstanceLabelUniqueness(const TempoSensors::SetInstanceLabelUniquenessRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void HandleSetLabelRowOverrides(const TempoSensors::SetLabelRowOverridesRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	const TSet<FName>& GetLabeledActorClassNames() const { return LabeledActorClassNames; }

	TMap<uint8, uint8> GetInstanceToSemanticIdMap() const;

protected:
	void BuildLabelMaps();

	void LabelAllActors();

	void LabelActor(AActor* Actor);

	void LabelAllComponents(const AActor* Actor, FInstanceSemanticIdPair ActorIdPair);

	void LabelComponent(UActorComponent* Component);

	void LabelComponent(UPrimitiveComponent* Component, FInstanceSemanticIdPair ActorIdPair);

	// Resolves the label an Actor earns, honoring runtime overrides over the label table. Unset
	// means nothing in the table matches the Actor and it should go unlabeled.
	TOptional<int32> ResolveActorSemanticId(const AActor* Actor) const;

	// Resolves the label a Component earns in its own right, independent of its owning Actor.
	// Unset means the Component has no label of its own and should inherit its Actor's.
	TOptional<int32> ResolveComponentSemanticId(const UPrimitiveComponent* Component) const;

	// Resolves the label a mesh asset path earns, static or skeletal, honoring runtime overrides
	// over the label table.
	TOptional<int32> ResolveMeshSemanticId(const FString& MeshPath) const;

	// The row name a semantic ID belongs to, for reporting an ID back as a class. Unset when no row
	// carries the ID, which a runtime override to an ID the table doesn't define can produce.
	TOptional<FName> ResolveSemanticIdRowName(int32 SemanticId) const;

	// Shared body of the static and skeletal SetMeshTypeSemanticId RPCs: record or clear the
	// override in the given map, then re-label every component rendering that mesh. The caller has
	// already validated the ID range and that the path names an asset of its map's type.
	void SetMeshTypeSemanticIdOverride(const FString& MeshPath, int32 SemanticId, TMap<FString, int32>& Overrides);

	// Shared body of the GetAllStaticMeshTypes and GetAllSkeletalMeshTypes RPCs: count the world's
	// components rendering each mesh of the requested kind, keyed by asset path.
	void CountMeshInstances(bool bSkeletal, TMap<FString, int32>& OutMeshInstanceCounts) const;

	// The mesh assets a Component renders: its own static or skeletal mesh, or the meshes a Niagara
	// system instances.
	static void GetComponentMeshPaths(const UPrimitiveComponent* Component, TArray<FString>& OutMeshPaths);

	void UnLabelAllActors();

	void UnLabelActor(AActor* Actor);

	void UnLabelAllComponents(const AActor* Actor);

	void UnLabelComponent(UActorComponent* Component);

	void UnLabelComponent(UPrimitiveComponent* Component);

	// The allocator applied through the instance label uniqueness settings. Both read the settings
	// live, so a change governs only the allocations and returns that follow it.
	TOptional<int32> AllocateInstanceId();

	void ReturnInstanceId(int32 InstanceId);

	// Re-derive everything cached from the semantic label table, then re-label the world from
	// scratch. Bound to the settings' label-settings-changed event.
	void OnLabelSettingsChanged();

	static void AssignId(UPrimitiveComponent* Component, FInstanceSemanticIdPair IdPair);

	UPROPERTY(VisibleAnywhere)
	UDataTable* SemanticLabelTable;

	UPROPERTY(VisibleAnywhere)
	TMap<TSubclassOf<AActor>, FName> ActorSemanticLabels;

	UPROPERTY(VisibleAnywhere)
	TMap<FString, FName> StaticMeshLabels;

	UPROPERTY(VisibleAnywhere)
	TMap<FString, FName> SkeletalMeshLabels;

	UPROPERTY(VisibleAnywhere)
	TMap<FName, FName> ComponentTagLabels;

	UPROPERTY(VisibleAnywhere)
	TMap<FName, FName> ActorTagLabels;

	UPROPERTY(VisibleAnywhere)
	TMap<FName, int32> SemanticIds;

	UPROPERTY()
	TMap<const UObject*, FInstanceSemanticIdPair> LabeledObjects;

	// Set of actor class names that have been assigned unique instance IDs
	UPROPERTY()
	TSet<FName> LabeledActorClassNames;

	// Runtime overrides for actor types (real UClass name -> semantic ID)
	// Takes precedence over DataTable definitions
	UPROPERTY()
	TMap<FName, int32> ActorTypeSemanticIdOverrides;

	// Runtime overrides for static mesh types (full mesh path -> semantic ID)
	// Takes precedence over DataTable definitions (StaticMeshLabels)
	UPROPERTY()
	TMap<FString, int32> StaticMeshTypeSemanticIdOverrides;

	// Runtime overrides for skeletal mesh types (full mesh path -> semantic ID)
	// Takes precedence over DataTable definitions (SkeletalMeshLabels)
	// Kept separate from the static map so each reporting RPC can list exactly what its setter
	// accepts. The two never hold the same path: an asset is a UStaticMesh or a USkeletalMesh, and
	// each setter rejects a path that isn't its own type.
	UPROPERTY()
	TMap<FString, int32> SkeletalMeshTypeSemanticIdOverrides;

	// Runtime overrides for actor tags (tag -> semantic ID)
	// Takes precedence over DataTable definitions (ActorTagLabels)
	UPROPERTY()
	TMap<FName, int32> ActorTagSemanticIdOverrides;

	// 0 is reserved for "unlabeled", and the camera cannot encode an ID above
	// GTempoCamera_Max_Label, so instance IDs run 1..GTempoCamera_Max_Label.
	FInstanceIdAllocator InstanceIdAllocator = FInstanceIdAllocator(1, GTempoCamera_Max_Label);
};
