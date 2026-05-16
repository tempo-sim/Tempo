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

	const TSet<FName>& GetLabeledActorClassNames() const { return LabeledActorClassNames; }

	TMap<uint8, uint8> GetInstanceToSemanticIdMap() const;

protected:
	void BuildLabelMaps();

	void LabelAllActors();

	void LabelActor(AActor* Actor);

	void LabelAllComponents(const AActor* Actor, FInstanceSemanticIdPair ActorIdPair);

	void LabelComponent(UActorComponent* Component);

	void LabelComponent(UPrimitiveComponent* Component, FInstanceSemanticIdPair ActorIdPair);

	void UnLabelAllActors();

	void UnLabelActor(AActor* Actor);

	void UnLabelAllComponents(const AActor* Actor);

	void UnLabelComponent(UActorComponent* Component);

	void UnLabelComponent(UPrimitiveComponent* Component);

	void ReLabelAllActors();

	static void AssignId(UPrimitiveComponent* Component, FInstanceSemanticIdPair IdPair);

	UPROPERTY(VisibleAnywhere)
	UDataTable* SemanticLabelTable;

	UPROPERTY(VisibleAnywhere)
	TMap<TSubclassOf<AActor>, FName> ActorSemanticLabels;

	UPROPERTY(VisibleAnywhere)
	TMap<FString, FName> StaticMeshLabels;

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

	// Runtime overrides for actor types (class name -> semantic ID)
	// Takes precedence over DataTable definitions
	UPROPERTY()
	TMap<FName, int32> ActorTypeSemanticIdOverrides;

	// Runtime overrides for static mesh types (full mesh path -> semantic ID)
	// Takes precedence over DataTable definitions (StaticMeshLabels)
	UPROPERTY()
	TMap<FString, int32> StaticMeshTypeSemanticIdOverrides;

	FInstanceIdAllocator InstanceIdAllocator = FInstanceIdAllocator(1, 255);
};
