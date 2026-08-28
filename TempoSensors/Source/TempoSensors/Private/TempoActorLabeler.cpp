// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorLabeler.h"

#include "TempoSensors.h"
#include "TempoLabelTypes.h"
#include "TempoInstancedStaticMeshComponent.h"
#include "TempoSensors/Labels.grpc.pb.h"

#include "TempoSensorsSettings.h"

#include "TempoClassUtils.h"
#include "TempoCoreUtils.h"
#include "DefaultActorClassifier.h"

#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSystem.h"

FInstanceIdAllocator::FInstanceIdAllocator(int32 MinIdIn, int32 MaxIdIn)
	: MinId(MinIdIn), MaxId(MaxIdIn)
{
	TSet<int32>& Ids = AvailableIds.AddDefaulted_GetRef();
	for (int32 I = MinId; I <= MaxId; ++I)
	{
		Ids.Add(I);
	}
}

TOptional<int32> FInstanceIdAllocator::Allocate()
{
	for (TSet<int32>& Ids : AvailableIds)
	{
		if (auto IdIt = Ids.CreateIterator())
		{
			const int32 Id = *IdIt;
			IdIt.RemoveCurrent();
			return Id;
		}
	}

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	if (TempoSensorsSettings->GetInstantaneouslyUniqueInstanceLabels())
	{
		// We are not allowed to reuse allocated IDs
		return TOptional<int32>();
	}

	// Reuse allocated IDs by making them available, at a higher count
	TSet<int32>& NextAvailableIds = AvailableIds.AddDefaulted_GetRef();
	for (int32 I = MinId + 1; I <= MaxId; ++I)
	{
		NextAvailableIds.Add(I);
	}

	// We reserved MinId above to be the one we will allocate
	return MinId;
}

void FInstanceIdAllocator::Return(int32 Id)
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	if (TempoSensorsSettings->GetGloballyUniqueInstanceLabels())
	{
		// We are not allowed to reuse IDs once they have been allocated
		return;
	}
	if (!ensureMsgf(Id >= MinId && Id <= MaxId, TEXT("Reclaimed Available Id %d outside original min/max"), Id))
	{
		return;
	}
	for (auto AvailableIdsIt = AvailableIds.CreateIterator(); AvailableIdsIt; ++AvailableIdsIt)
	{
		if (AvailableIdsIt->Contains(Id))
		{
			AvailableIdsIt->Remove(Id);
			// If this wasn't the first element in AvailableIds, make this ID available at the lower count
			if (auto PrevAvailableIdsIt = AvailableIdsIt - 1)
			{
				PrevAvailableIdsIt->Add(Id);
			}
			// If that was the last ID in the group, we don't need this group anymore.
			if (AvailableIdsIt->IsEmpty())
			{
				AvailableIdsIt.RemoveCurrent();
			}
			return;
		}
	}

	// If we never found the ID at a higher count, make sure we mark it available at count 0
	if (ensureMsgf(AvailableIds.Num() > 0, TEXT("AvailableIds was empty!")))
	{
		AvailableIds[0].Add(Id);
	}
}

using LabelService = TempoSensors::LabelService;
using LabelAsyncService = TempoSensors::LabelService::AsyncService;

void UTempoActorLabeler::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<LabelService>(
		SimpleRequestHandler(&LabelAsyncService::RequestGetInstanceToSemanticIdMap, &UTempoActorLabeler::GetInstanceToSemanticIdMap),
		SimpleRequestHandler(&LabelAsyncService::RequestGetAllActorLabels, &UTempoActorLabeler::HandleGetAllActorLabels),
		SimpleRequestHandler(&LabelAsyncService::RequestGetLabeledActorTypes, &UTempoActorLabeler::HandleGetLabeledActorTypes),
		SimpleRequestHandler(&LabelAsyncService::RequestGetSemanticClasses, &UTempoActorLabeler::HandleGetSemanticClasses),
		SimpleRequestHandler(&LabelAsyncService::RequestSetActorTypeSemanticId, &UTempoActorLabeler::HandleSetActorTypeSemanticId),
		SimpleRequestHandler(&LabelAsyncService::RequestGetAllStaticMeshTypes, &UTempoActorLabeler::HandleGetAllStaticMeshTypes),
		SimpleRequestHandler(&LabelAsyncService::RequestSetStaticMeshTypeSemanticId, &UTempoActorLabeler::HandleSetStaticMeshTypeSemanticId)
	);
}

void UTempoActorLabeler::HandleGetLabeledActorTypes(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetLabeledActorTypesResponse>& ResponseContinuation)
{
	TempoSensors::GetLabeledActorTypesResponse Response;

	// Check if instance ID mode is enabled. If not, this request is not applicable.
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	if (TempoSensorsSettings->GetLabelType() != ELabelType::Instance)
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Instance label mode is not enabled"));
		return;
	}

	for (const FName& ClassName : LabeledActorClassNames)
	{
		Response.add_types(TCHAR_TO_UTF8(*ClassName.ToString()));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorLabeler::HandleGetSemanticClasses(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetSemanticClassesResponse>& ResponseContinuation)
{
	TempoSensors::GetSemanticClassesResponse Response;

	// Build reverse mapping: semantic_id -> actor types
	TMap<int32, TArray<FName>> SemanticIdToActorTypes;

	// Include DataTable assignments
	for (const auto& [ActorClass, LabelName] : ActorSemanticLabels)
	{
		if (const int32* SemanticId = SemanticIds.Find(LabelName))
		{
			SemanticIdToActorTypes.FindOrAdd(*SemanticId).Add(ActorClass->GetFName());
		}
	}

	// Include runtime overrides (they take precedence)
	for (const auto& [ActorTypeName, OverrideSemanticId] : ActorTypeSemanticIdOverrides)
	{
		// Remove from old mapping if present, add to new
		for (auto& [Id, Types] : SemanticIdToActorTypes)
		{
			Types.Remove(ActorTypeName);
		}
		SemanticIdToActorTypes.FindOrAdd(OverrideSemanticId).Add(ActorTypeName);
	}

	// Build reverse mapping: semantic_id -> static mesh paths
	TMap<int32, TArray<FString>> SemanticIdToMeshPaths;

	// Include DataTable static mesh assignments
	for (const auto& [MeshPath, LabelName] : StaticMeshLabels)
	{
		if (const int32* SemanticId = SemanticIds.Find(LabelName))
		{
			SemanticIdToMeshPaths.FindOrAdd(*SemanticId).Add(MeshPath);
		}
	}

	// Include runtime static mesh overrides (they take precedence)
	for (const auto& [MeshPath, OverrideSemanticId] : StaticMeshTypeSemanticIdOverrides)
	{
		// Remove from old mapping if present, add to new
		for (auto& [Id, Paths] : SemanticIdToMeshPaths)
		{
			Paths.Remove(MeshPath);
		}
		SemanticIdToMeshPaths.FindOrAdd(OverrideSemanticId).Add(MeshPath);
	}

	// Build reverse mapping: semantic_id -> component tags
	TMap<int32, TArray<FName>> SemanticIdToComponentTags;

	for (const auto& [ComponentTag, LabelName] : ComponentTagLabels)
	{
		if (const int32* SemanticId = SemanticIds.Find(LabelName))
		{
			SemanticIdToComponentTags.FindOrAdd(*SemanticId).Add(ComponentTag);
		}
	}

	// Iterate DataTable to get all class definitions
	for (const auto& [LabelName, SemanticId] : SemanticIds)
	{
		auto* ClassInfo = Response.add_classes();
		ClassInfo->set_name(TCHAR_TO_UTF8(*LabelName.ToString()));
		ClassInfo->set_label_id(SemanticId);

		if (TArray<FName>* Types = SemanticIdToActorTypes.Find(SemanticId))
		{
			for (const FName& TypeName : *Types)
			{
				ClassInfo->add_actor_types(TCHAR_TO_UTF8(*TypeName.ToString()));
			}
		}

		if (TArray<FString>* MeshPaths = SemanticIdToMeshPaths.Find(SemanticId))
		{
			for (const FString& MeshPath : *MeshPaths)
			{
				ClassInfo->add_static_mesh_types(TCHAR_TO_UTF8(*MeshPath));
			}
		}

		if (TArray<FName>* ComponentTags = SemanticIdToComponentTags.Find(SemanticId))
		{
			for (const FName& ComponentTag : *ComponentTags)
			{
				ClassInfo->add_component_tags(TCHAR_TO_UTF8(*ComponentTag.ToString()));
			}
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorLabeler::HandleSetActorTypeSemanticId(const TempoSensors::SetActorTypeSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	const int32 SemanticId = Request.semantic_id();

	// Validate range
	if (SemanticId < -1 || SemanticId > 255)
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
			"semantic_id must be -1 (revert) or 0-255"));
		return;
	}

	// Resolve to an actual class so that either spelling of a Blueprint class name is accepted, the
	// override is keyed on the real class name we report elsewhere, and a name we can't place is
	// rejected rather than silently registering an override no actor will ever match.
	const FString ActorTypeName(UTF8_TO_TCHAR(Request.actor_type().c_str()));
	const UClass* ActorClass = GetSubClassWithName<AActor>(ActorTypeName);
	if (!ActorClass)
	{
		const FString ErrorMsg = FString::Printf(TEXT("No actor class with name '%s' found"), *ActorTypeName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}
	const FName ActorType = ActorClass->GetFName();

	// Store or clear override
	if (SemanticId < 0)
	{
		ActorTypeSemanticIdOverrides.Remove(ActorType);
	}
	else
	{
		ActorTypeSemanticIdOverrides.Add(ActorType, SemanticId);
	}

	// Re-label all existing actors of this type
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		if (ActorItr->GetClass()->GetFName() == ActorType)
		{
			UnLabelActor(*ActorItr);
			LabelActor(*ActorItr);
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleGetAllStaticMeshTypes(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetAllStaticMeshTypesResponse>& ResponseContinuation)
{
	TempoSensors::GetAllStaticMeshTypesResponse Response;

	// Build map of mesh paths to instance counts
	TMap<FString, int32> MeshInstanceCounts;

	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AActor* Actor = *ActorItr;

		// 1. Handle regular UStaticMeshComponent (non-instanced)
		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(Actor);
		for (UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			// Skip ISMCs - they'll be handled separately below
			if (Cast<UInstancedStaticMeshComponent>(MeshComponent))
			{
				continue;
			}

			if (const UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
			{
				const FString MeshFullPath = StaticMesh->GetPathName();
				MeshInstanceCounts.FindOrAdd(MeshFullPath)++;
			}
		}

		// 2. Handle UInstancedStaticMeshComponent (PCG, foliage, etc.)
		// ISMCs store multiple instances of the same mesh
		TInlineComponentArray<UInstancedStaticMeshComponent*> ISMComponents(Actor);
		for (UInstancedStaticMeshComponent* ISMC : ISMComponents)
		{
			if (const UStaticMesh* StaticMesh = ISMC->GetStaticMesh())
			{
				const FString MeshFullPath = StaticMesh->GetPathName();
				MeshInstanceCounts.FindOrAdd(MeshFullPath) += ISMC->GetInstanceCount();
			}
		}

		// 3. Handle the meshes a Niagara mesh renderer instances. The live particle count varies
		// every frame, so count the components drawing the mesh rather than the particles.
		TInlineComponentArray<UNiagaraComponent*> NiagaraComponents(Actor);
		for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
		{
			TArray<FString> MeshPaths;
			GetComponentStaticMeshPaths(NiagaraComponent, MeshPaths);
			for (const FString& MeshPath : MeshPaths)
			{
				MeshInstanceCounts.FindOrAdd(MeshPath)++;
			}
		}
	}

	// Build response with mesh info
	for (const auto& [MeshPath, InstanceCount] : MeshInstanceCounts)
	{
		auto* MeshInfo = Response.add_mesh_types();
		MeshInfo->set_mesh_path(TCHAR_TO_UTF8(*MeshPath));

		// Extract display name from path (e.g., "/Game/Meshes/SM_Tree.SM_Tree" -> "SM_Tree")
		FString DisplayName = FPaths::GetBaseFilename(MeshPath);
		MeshInfo->set_display_name(TCHAR_TO_UTF8(*DisplayName));

		MeshInfo->set_instance_count(InstanceCount);

		// Determine current semantic ID: check overrides first, then DataTable
		MeshInfo->set_current_semantic_id(ResolveStaticMeshSemanticId(MeshPath).Get(-1));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorLabeler::HandleSetStaticMeshTypeSemanticId(const TempoSensors::SetStaticMeshTypeSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	const FString MeshPath = UTF8_TO_TCHAR(Request.static_mesh_path().c_str());
	const int32 SemanticId = Request.semantic_id();

	// Validate range
	if (SemanticId < -1 || SemanticId > 255)
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
			"semantic_id must be -1 (revert) or 0-255"));
		return;
	}

	// Store or clear override
	if (SemanticId < 0)
	{
		StaticMeshTypeSemanticIdOverrides.Remove(MeshPath);
	}
	else
	{
		StaticMeshTypeSemanticIdOverrides.Add(MeshPath, SemanticId);
	}

	// Re-label all components rendering this mesh, Niagara mesh renderers included.
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(*ActorItr);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			TArray<FString> MeshPaths;
			GetComponentStaticMeshPaths(PrimitiveComponent, MeshPaths);
			if (MeshPaths.Contains(MeshPath))
			{
				UnLabelComponent(PrimitiveComponent);
				if (const FInstanceSemanticIdPair* ActorIdPair = LabeledObjects.Find(*ActorItr))
				{
					LabelComponent(PrimitiveComponent, *ActorIdPair);
				}
			}
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleGetAllActorLabels(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetAllActorLabelsResponse>& ResponseContinuation)
{
	TempoSensors::GetAllActorLabelsResponse Response;

	for (const auto& LabeledObjectPair : LabeledObjects)
	{
		const AActor* Actor = Cast<AActor>(LabeledObjectPair.Key);
		if (!Actor)
		{
			continue;
		}

		const FInstanceSemanticIdPair& IdPair = LabeledObjectPair.Value;

		auto* ActorInfo = Response.add_actors();
		ActorInfo->set_actor_name(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Actor)));
		ActorInfo->set_actor_type(TCHAR_TO_UTF8(*Actor->GetClass()->GetName()));
		ActorInfo->set_semantic_id(IdPair.SemanticId);
		ActorInfo->set_instance_id(IdPair.InstanceId);
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

TMap<uint8, uint8> UTempoActorLabeler::GetInstanceToSemanticIdMap() const
{
	TMap<uint8, uint8> Result;
	for (const auto& LabeledObject : LabeledObjects)
	{
		if (LabeledObject.Value.InstanceId != NoLabelId)
		{
			Result.Add(LabeledObject.Value.InstanceId, LabeledObject.Value.SemanticId);
		}
	}
	return Result;
}

void UTempoActorLabeler::GetInstanceToSemanticIdMap(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::InstanceToSemanticIdMap>& ResponseContinuation) const
{
	TMap<uint8, uint8> Map = GetInstanceToSemanticIdMap();

	TempoSensors::InstanceToSemanticIdMap ProtoResponse;
	for (const auto& Pair : Map)
	{
		auto* ProtoPair = ProtoResponse.add_instance_semantic_id_pairs();
		ProtoPair->set_instance_id(Pair.Key);
		ProtoPair->set_semantic_id(Pair.Value);
	}
	ResponseContinuation.ExecuteIfBound(ProtoResponse, grpc::Status_OK);
}

void UTempoActorLabeler::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Only for game worlds
	if (!UTempoCoreUtils::IsGameWorld(&InWorld))
	{
		return;
	}

	SemanticLabelTable = GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable();

	// Parse the label table into a more convenient structure.
	BuildLabelMaps();

	// Label all actors *after* BeginPlay (UWorldSubsystem::OnWorldBeginPlay is called *before* BeginPlay).
	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoActorLabeler::LabelAllActors);

	// Label all newly spawned actors.
	GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UTempoActorLabeler::LabelActor));

	// UnLabel any destroyed actors.
	GetWorld()->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &UTempoActorLabeler::UnLabelActor));

	// Handles labeling or re-labeling any component whose render state is marked dirty (for example their mesh changed).
	UActorComponent::MarkRenderStateDirtyEvent.AddWeakLambda(this, [this](UActorComponent& Component)
	{
		LabelComponent(&Component);
	});

	// Handles labeling any component with a physics state who is created after their Actor is spawned.
	UActorComponent::GlobalCreatePhysicsDelegate.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		LabelComponent(Component);
	});

	// Handles un-labeling any component with a physics state who is destroyed although their Actor is not.
	UActorComponent::GlobalDestroyPhysicsDelegate.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		UnLabelComponent(Component);
	});

	// Handles labeling TempoInstancedStaticMeshComponents when they are registered.
	UTempoInstancedStaticMeshComponent::TempoInstancedStaticMeshRegisteredEvent.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		LabelComponent(Component);
	});

	// Handles un-labeling TempoInstancedStaticMeshComponents when they are unregistered.
	UTempoInstancedStaticMeshComponent::TempoInstancedStaticMeshUnRegisteredEvent.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		UnLabelComponent(Component);
	});

	GetMutableDefault<UTempoSensorsSettings>()->TempoSensorsLabelSettingsChangedEvent.AddUObject(this, &UTempoActorLabeler::ReLabelAllActors);
}

void UTempoActorLabeler::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<LabelService>(this);
}

void UTempoActorLabeler::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<LabelService>();
}

void UTempoActorLabeler::BuildLabelMaps()
{
	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoSensors, Error, TEXT("Semantic Label table was not set"));
		return;
	}

	SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""), [this](const FName& Key, const FSemanticLabel& Value)
	{
		const FName& Label = Key;
		for (const TSubclassOf<AActor>& ActorType : Value.ActorTypes)
		{
			if (ActorType.Get())
			{
				if (ActorSemanticLabels.Contains(ActorType))
				{
					UE_LOG(LogTempoSensors, Error, TEXT("Actor type %s is associated with more than one label (%s and %s)"), *ActorType->GetName(), *ActorSemanticLabels[ActorType].ToString(), *Label.ToString());
					continue;
				}
				ActorSemanticLabels.Add(ActorType, Label);
			}
			else
			{
				UE_LOG(LogTempoSensors, Warning, TEXT("Null Actor associated with label %s"), *Label.ToString());
			}
		}

		for (const TSoftObjectPtr<UStaticMesh>& StaticMeshAsset : Value.StaticMeshTypes)
		{
			if (const UStaticMesh* StaticMesh = StaticMeshAsset.LoadSynchronous())
			{
				const FString MeshFullPath = StaticMesh->GetPathName();
				if (StaticMeshLabels.Contains(MeshFullPath))
				{
					UE_LOG(LogTempoSensors, Error, TEXT("Static mesh type %s is associated with more than one label (%s and %s)"), *MeshFullPath, *StaticMeshLabels[MeshFullPath].ToString(), *Label.ToString());
					continue;
				}
				StaticMeshLabels.Add(MeshFullPath, Label);
			}
			else
			{
				UE_LOG(LogTempoSensors, Warning, TEXT("Null static mesh associated with label %s"), *Label.ToString());
			}
		}

		for (const FName& ComponentTag : Value.ComponentTags)
		{
			if (ComponentTag.IsNone())
			{
				UE_LOG(LogTempoSensors, Warning, TEXT("Empty component tag associated with label %s"), *Label.ToString());
				continue;
			}
			if (ComponentTagLabels.Contains(ComponentTag))
			{
				UE_LOG(LogTempoSensors, Error, TEXT("Component tag %s is associated with more than one label (%s and %s)"), *ComponentTag.ToString(), *ComponentTagLabels[ComponentTag].ToString(), *Label.ToString());
				continue;
			}
			ComponentTagLabels.Add(ComponentTag, Label);
		}

		const int32 LabelId = Value.Label;
		if (SemanticIds.Contains(Label))
		{
			UE_LOG(LogTempoSensors, Error, TEXT("Label name %s is associated with more than one label ID (%d and %d)"), *Label.ToString(), SemanticIds[Label], LabelId);
		}
		else
		{
			SemanticIds.Add(Label, LabelId);
		}
	});

	if (const int32* NoLabelIdPtr = SemanticIds.Find(NoLabelName))
	{
		NoLabelId = *NoLabelIdPtr;
	}
	else
	{
		UE_LOG(LogTempoSensors, Error, TEXT("Label Table did not contain entry for NoLabel name"));
	}
}

void UTempoActorLabeler::LabelAllActors()
{
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		LabelActor(*ActorItr);
	}
}

void UTempoActorLabeler::LabelActor(AActor* Actor)
{
	if (const FInstanceSemanticIdPair* ActorIdPairPtr = LabeledObjects.Find(Actor))
	{
		// We've labeled this Actor before. Make sure all the components are labeled.
		// Copy to avoid dangling reference if LabeledObjects is modified during labeling.
		FInstanceSemanticIdPair ActorIdPair = *ActorIdPairPtr;
		LabelAllComponents(Actor, ActorIdPair);
		return;
	}

	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoSensors, Error, TEXT("Semantic Label table was not set"));
		return;
	}

	const FName ActorTypeName = Actor->GetClass()->GetFName();

	// Check for type-level override first (before DataTable lookup)
	if (const int32* TypeOverride = ActorTypeSemanticIdOverrides.Find(ActorTypeName))
	{
		FInstanceSemanticIdPair ActorIdPair;
		ActorIdPair.SemanticId = *TypeOverride;
		if (TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
		{
			ActorIdPair.InstanceId = *InstanceId;
			// Track actor class names that have been assigned instance IDs
			if (GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance)
			{
				LabeledActorClassNames.Add(ActorTypeName);
			}
		}
		LabeledObjects.Add(Actor, ActorIdPair);
		LabelAllComponents(Actor, ActorIdPair);
		return;
	}

	FInstanceSemanticIdPair ActorIdPair;
	FName AssignedLabel = NoLabelName;
	for (const auto& Elem : ActorSemanticLabels)
	{
		const TSubclassOf<AActor>& ActorType = Elem.Key;
		const FName& ActorLabel = Elem.Value;
		if (Actor->GetClass()->IsChildOf(ActorType.Get()))
		{
			if (const int32* SemanticId = SemanticIds.Find(ActorLabel))
			{
				if (AssignedLabel != NoLabelName && *ActorLabel.ToString() != AssignedLabel)
				{
					UE_LOG(LogTempoSensors, Error, TEXT("Labels %s and %s have overlapping actor types"), *ActorLabel.ToString(), *AssignedLabel.ToString());
					continue;
				}
				AssignedLabel = ActorLabel;
				if (TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
				{
					ActorIdPair.InstanceId = *InstanceId;
					// Track actor class names that have been assigned instance IDs
					if (GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance)
					{
						LabeledActorClassNames.Add(ActorTypeName);
					}
				}
				ActorIdPair.SemanticId = *SemanticId;
			}
			else
			{
				UE_LOG(LogTempoSensors, Error, TEXT("Label %s did not have an associated ID"), *ActorLabel.ToString());
			}
		}
	}

	LabeledObjects.Add(Actor, ActorIdPair);

	LabelAllComponents(Actor, ActorIdPair);
}

void UTempoActorLabeler::LabelAllComponents(const AActor* Actor, FInstanceSemanticIdPair ActorIdPair)
{
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		LabelComponent(PrimitiveComponent, ActorIdPair);
	}
}

void UTempoActorLabeler::LabelComponent(UActorComponent* Component)
{
	if (!IsValid(Component) || !Component->IsValidLowLevel() || Component->IsBeingDestroyed() || !Component->GetOwner() || !UTempoCoreUtils::IsGameWorld(Component))
	{
		return;
	}

	if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
	{
		if (const FInstanceSemanticIdPair* ActorIdPairPtr = LabeledObjects.Find(PrimitiveComponent->GetOwner()))
		{
			// Copy to avoid dangling reference if LabeledObjects is modified during labeling.
			FInstanceSemanticIdPair ActorIdPair = *ActorIdPairPtr;
			LabelComponent(PrimitiveComponent, ActorIdPair);
			return;
		}

		// We've never labeled this component's owner, label the whole Actor instead of just this component.
		LabelActor(PrimitiveComponent->GetOwner());
	}
}

void UTempoActorLabeler::LabelComponent(UPrimitiveComponent* Component, FInstanceSemanticIdPair ActorIdPair)
{
	if (const TOptional<int32> ComponentSemanticId = ResolveComponentSemanticId(Component))
	{
		if (LabeledObjects.Contains(Component))
		{
			// This component is already labeled.
			return;
		}

		FInstanceSemanticIdPair IdPair;
		IdPair.SemanticId = *ComponentSemanticId;
		if (TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
		{
			IdPair.InstanceId = *InstanceId;
		}

		// Label using the component's own label rather than the owning Actor's.
		LabeledObjects.Add(Component, IdPair);
		AssignId(Component, IdPair);
		return;
	}

	// No component label found. Label with its owning Actor's label.
	LabeledObjects.Add(Component, ActorIdPair);
	AssignId(Component, ActorIdPair);
}

TOptional<int32> UTempoActorLabeler::ResolveComponentSemanticId(const UPrimitiveComponent* Component) const
{
	// Most specific rule wins. A component tag names one particular component, so it beats the
	// mesh rules, which name an asset every component sharing that asset is subject to.
	for (const FName& ComponentTag : Component->ComponentTags)
	{
		if (const FName* TagLabel = ComponentTagLabels.Find(ComponentTag))
		{
			if (const int32* TagLabelId = SemanticIds.Find(*TagLabel))
			{
				return *TagLabelId;
			}
			UE_LOG(LogTempoSensors, Error, TEXT("Label %s did not have an associated ID"), *TagLabel->ToString());
		}
	}

	TArray<FString> MeshPaths;
	GetComponentStaticMeshPaths(Component, MeshPaths);
	for (const FString& MeshPath : MeshPaths)
	{
		if (const TOptional<int32> MeshSemanticId = ResolveStaticMeshSemanticId(MeshPath))
		{
			return MeshSemanticId;
		}
	}

	return TOptional<int32>();
}

TOptional<int32> UTempoActorLabeler::ResolveStaticMeshSemanticId(const FString& MeshPath) const
{
	// Runtime overrides take precedence over the label table.
	if (const int32* OverrideSemanticId = StaticMeshTypeSemanticIdOverrides.Find(MeshPath))
	{
		return *OverrideSemanticId;
	}

	if (const FName* StaticMeshLabel = StaticMeshLabels.Find(MeshPath))
	{
		if (const int32* StaticMeshLabelId = SemanticIds.Find(*StaticMeshLabel))
		{
			return *StaticMeshLabelId;
		}
		UE_LOG(LogTempoSensors, Error, TEXT("Label %s did not have an associated ID"), *StaticMeshLabel->ToString());
	}

	return TOptional<int32>();
}

void UTempoActorLabeler::GetComponentStaticMeshPaths(const UPrimitiveComponent* Component, TArray<FString>& OutMeshPaths)
{
	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		if (const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
		{
			OutMeshPaths.Add(StaticMesh->GetPathName());
		}
		return;
	}

	// A Niagara system's mesh renderers instance static meshes that belong to no component of their
	// own, so gather them from the asset. Every particle the component draws shares one stencil
	// value (it is one scene proxy), so the first labeled mesh decides the whole component's label.
	if (const UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(Component))
	{
		const UNiagaraSystem* NiagaraSystem = NiagaraComponent->GetAsset();
		if (!NiagaraSystem)
		{
			return;
		}

		for (const FNiagaraEmitterHandle& EmitterHandle : NiagaraSystem->GetEmitterHandles())
		{
			if (!EmitterHandle.GetIsEnabled())
			{
				continue;
			}
			const FVersionedNiagaraEmitterData* EmitterData = EmitterHandle.GetEmitterData();
			if (!EmitterData)
			{
				continue;
			}
			for (const UNiagaraRendererProperties* RendererProperties : EmitterData->GetRenderers())
			{
				const UNiagaraMeshRendererProperties* MeshRenderer = Cast<UNiagaraMeshRendererProperties>(RendererProperties);
				if (!MeshRenderer)
				{
					continue;
				}
				for (const FNiagaraMeshRendererMeshProperties& MeshProperties : MeshRenderer->Meshes)
				{
					if (MeshProperties.Mesh)
					{
						OutMeshPaths.AddUnique(MeshProperties.Mesh->GetPathName());
					}
				}
			}
		}
	}
}

void UTempoActorLabeler::UnLabelAllActors()
{
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		UnLabelActor(*ActorItr);
	}

	// Clear the set of labeled actor class names
	LabeledActorClassNames.Empty();
}

void UTempoActorLabeler::UnLabelActor(AActor* Actor)
{
	if (!LabeledObjects.Contains(Actor))
	{
		// We've never labeled this Actor.
		return;
	}

	UnLabelAllComponents(Actor);

	if (GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance)
	{
		if (const FInstanceSemanticIdPair* IdPair = LabeledObjects.Find(Actor); IdPair->InstanceId != NoLabelId)
		{
			InstanceIdAllocator.Return(IdPair->InstanceId);
		}
	}

	LabeledObjects.Remove(Actor);
}

void UTempoActorLabeler::UnLabelAllComponents(const AActor* Actor)
{
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		UnLabelComponent(PrimitiveComponent);
	}
}

void UTempoActorLabeler::UnLabelComponent(UActorComponent* Component)
{
	if (!Component->GetOwner() || !UTempoCoreUtils::IsGameWorld(Component))
	{
		return;
	}

	if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
	{
		UnLabelComponent(PrimitiveComponent);
	}
}

void UTempoActorLabeler::UnLabelComponent(UPrimitiveComponent* Component)
{
	Component->SetRenderCustomDepth(false);
	Component->SetCustomDepthStencilValue(0);

	if (GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance)
	{
		if (const FInstanceSemanticIdPair* ComponentIdPair = LabeledObjects.Find(Component))
		{
			const FInstanceSemanticIdPair* ActorIdPair = LabeledObjects.Find(Component->GetOwner());
			if (!ActorIdPair || (ActorIdPair->InstanceId != ComponentIdPair->InstanceId && ComponentIdPair->InstanceId != NoLabelId))
			{
				InstanceIdAllocator.Return(ComponentIdPair->InstanceId);
			}
		}
	}

	LabeledObjects.Remove(Component);
}

void UTempoActorLabeler::ReLabelAllActors()
{
	UnLabelAllActors();
	LabelAllActors();
}

void UTempoActorLabeler::AssignId(UPrimitiveComponent* Component, FInstanceSemanticIdPair IdPair)
{
	if (!Component->bRenderCustomDepth)
	{
		Component->SetRenderCustomDepth(true);
	}
	const int32 StencilValue = GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance ? IdPair.InstanceId : IdPair.SemanticId;
	if (Component->CustomDepthStencilValue != StencilValue)
	{
		Component->SetCustomDepthStencilValue(StencilValue);
	}
}

FName UTempoActorLabeler::GetActorClassification(const AActor* Actor) const
{
	for (const auto& Elem : ActorSemanticLabels)
	{
		const TSubclassOf<AActor>& ActorType = Elem.Key;
		const FName& ActorLabel = Elem.Value;
		if (Actor->GetClass()->IsChildOf(ActorType.Get()))
		{
			return ActorLabel;
		}
	}

	return UDefaultActorClassifier::GetDefaultActorClassification(Actor);
}
