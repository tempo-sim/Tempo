// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "ActorClassificationInterface.h"

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
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

namespace TempoScripting
{
	class Empty;
}

namespace TempoLabels
{
	class InstanceToSemanticIdMap;
	class GetLabeledActorTypesRequest;
	class GetLabeledActorTypesResponse;
}

/**
 * Tags all meshes on all Actors in the world with the appropriate label.
 */
UCLASS()
class TEMPOLABELS_API UTempoActorLabeler : public UTempoGameWorldSubsystem, public IActorClassificationInterface, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	virtual FName GetActorClassification(const AActor* Actor) const override;

	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	void GetInstanceToSemanticIdMap(const TempoScripting::Empty& Request, const TResponseDelegate<TempoLabels::InstanceToSemanticIdMap>& ResponseContinuation);

	void HandleGetLabeledActorTypes(const TempoLabels::GetLabeledActorTypesRequest& Request, const TResponseDelegate<TempoLabels::GetLabeledActorTypesResponse>& ResponseContinuation);

	const TSet<FName>& GetLabeledActorClassNames() const { return LabeledActorClassNames; }

	void GetInstanceToSemanticIdMap(TMap<uint8, uint8>& OutMap) const;

protected:
	void BuildLabelMaps();

	void LabelAllActors();

	void LabelActor(AActor* Actor);

	void LabelAllComponents(const AActor* Actor, const FInstanceSemanticIdPair& ActorIdPair);

	void LabelComponent(UActorComponent* Component);

	void LabelComponent(UPrimitiveComponent* Component, const FInstanceSemanticIdPair& ActorIdPair);

	void UnLabelAllActors();

	void UnLabelActor(AActor* Actor);

	void UnLabelAllComponents(const AActor* Actor);

	void UnLabelComponent(UActorComponent* Component);

	void UnLabelComponent(UPrimitiveComponent* Component);

	void ReLabelAllActors();

	static void AssignId(UPrimitiveComponent* Component, const FInstanceSemanticIdPair& IdPair);

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

	FInstanceIdAllocator InstanceIdAllocator = FInstanceIdAllocator(1, 255);
};
