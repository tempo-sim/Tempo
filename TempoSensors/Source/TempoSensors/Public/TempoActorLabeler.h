// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "ActorClassificationInterface.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

#include "TempoActorLabeler.generated.h"

struct FInstanceIdAllocator
{
	FInstanceIdAllocator() = default;
	FInstanceIdAllocator(int32 MinIdIn, int32 MaxIdIn);

	TOptional<int32> Allocate();

	void Return(int32 Id);

private:
	int32 MinId, MaxId;
	// Array of available IDs, where each element is the IDs that have been allocated that index's number of times.
	// For example, at the beginning this has one element, index 0, with all available IDs.
	// IDs are always allocated from the lowest-count element.
	// Once all IDs have been allocated once, another element is added, with all available IDs.
	// When an ID is returned, if reusing IDs is allowed, the ID is moved from its current count to the next lower one.
	TArray<TSet<int32>> AvailableIds;
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

	// The mesh assets a Component renders: its own static or skeletal mesh, or the meshes a Niagara
	// system instances.
	static void GetComponentMeshPaths(const UPrimitiveComponent* Component, TArray<FString>& OutMeshPaths);

	void UnLabelAllActors();

	void UnLabelActor(AActor* Actor);

	void UnLabelAllComponents(const AActor* Actor);

	void UnLabelComponent(UActorComponent* Component);

	void UnLabelComponent(UPrimitiveComponent* Component);

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

	UPROPERTY(VisibleAnywhere)
	FName NoLabelName = TEXT("NoLabel");

	UPROPERTY(VisibleAnywhere)
	int32 NoLabelId = 0;

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

	// Runtime overrides for actor tags (tag -> semantic ID)
	// Takes precedence over DataTable definitions (ActorTagLabels)
	UPROPERTY()
	TMap<FName, int32> ActorTagSemanticIdOverrides;

	FInstanceIdAllocator InstanceIdAllocator = FInstanceIdAllocator(1, 255);
};
