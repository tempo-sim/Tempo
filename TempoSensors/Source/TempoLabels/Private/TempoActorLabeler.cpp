// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorLabeler.h"

#include "TempoLabels.h"
#include "TempoLabelTypes.h"
#include "TempoInstancedStaticMeshComponent.h"
#include "TempoLabels/Labels.grpc.pb.h"

#include "TempoSensorsSettings.h"

#include "TempoCoreUtils.h"
#include "DefaultActorClassifier.h"

#include "Engine.h"

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

using LabelService = TempoLabels::LabelService;
using LabelAsyncService = TempoLabels::LabelService::AsyncService;

void UTempoActorLabeler::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<LabelService>(
		SimpleRequestHandler(&LabelAsyncService::RequestGetInstanceToSemanticIdMap, &UTempoActorLabeler::GetInstanceToSemanticIdMap)
	);
}

void UTempoActorLabeler::GetInstanceToSemanticIdMap(const TempoScripting::Empty& Request, const TResponseDelegate<TempoLabels::InstanceToSemanticIdMap>& ResponseContinuation)
{
	TempoLabels::InstanceToSemanticIdMap InstanceToSemanticIdMap;
	for (const auto& LabeledObject : LabeledObjects)
	{
		if (LabeledObject.Value.InstanceId != NoLabelId)
		{
			auto* Pair = InstanceToSemanticIdMap.add_instance_semantic_id_pairs();
			Pair->set_instanceid(LabeledObject.Value.InstanceId);
			Pair->set_semanticid(LabeledObject.Value.SemanticId);
		}
	}
	ResponseContinuation.ExecuteIfBound(InstanceToSemanticIdMap, grpc::Status_OK);
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
	GetWorld()->AddOnActorDestroyedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UTempoActorLabeler::UnLabelActor));

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

	FTempoScriptingServer::Get().ActivateService<LabelService>(this);
}

void UTempoActorLabeler::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<LabelService>();
}

void UTempoActorLabeler::BuildLabelMaps()
{
	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoLabels, Error, TEXT("Semantic Label table was not set"));
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
					UE_LOG(LogTempoLabels, Error, TEXT("Actor type %s is associated with more than one label (%s and %s)"), *ActorType->GetName(), *ActorSemanticLabels[ActorType].ToString(), *Label.ToString());
					continue;
				}
				ActorSemanticLabels.Add(ActorType, Label);
			}
			else
			{
				UE_LOG(LogTempoLabels, Warning, TEXT("Null Actor associated with label %s"), *Label.ToString());
			}
		}

		for (const TSoftObjectPtr<UStaticMesh>& StaticMeshAsset : Value.StaticMeshTypes)
		{
			if (const UStaticMesh* StaticMesh = StaticMeshAsset.LoadSynchronous())
			{
				const FString MeshFullPath = StaticMesh->GetPathName();
				if (StaticMeshLabels.Contains(MeshFullPath))
				{
					UE_LOG(LogTempoLabels, Error, TEXT("Static mesh type %s is associated with more than one label (%s and %s)"), *MeshFullPath, *StaticMeshLabels[MeshFullPath].ToString(), *Label.ToString());
					continue;
				}
				StaticMeshLabels.Add(MeshFullPath, Label);
			}
			else
			{
				UE_LOG(LogTempoLabels, Warning, TEXT("Null static mesh associated with label %s"), *Label.ToString());
			}
		}

		const int32 LabelId = Value.Label;
		if (SemanticIds.Contains(Label))
		{
			UE_LOG(LogTempoLabels, Error, TEXT("Label name %s is associated with more than one label ID (%d and %d)"), *Label.ToString(), SemanticIds[Label], LabelId);
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
		UE_LOG(LogTempoLabels, Error, TEXT("Label Table did not contain entry for NoLabel name"));
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
	if (const FInstanceSemanticIdPair* ActorIdPair = LabeledObjects.Find(Actor))
	{
		// We've labeled this Actor before. Make sure all the components are labeled.
		LabelAllComponents(Actor, *ActorIdPair);
		return;
	}

	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoLabels, Error, TEXT("Semantic Label table was not set"));
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
					UE_LOG(LogTempoLabels, Error, TEXT("Labels %s and %s have overlapping actor types"), *ActorLabel.ToString(), *AssignedLabel.ToString());
					continue;
				}
				AssignedLabel = ActorLabel;
				if (TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
				{
					ActorIdPair.InstanceId = *InstanceId;
				}
				ActorIdPair.SemanticId = *SemanticId;
			}
			else
			{
				UE_LOG(LogTempoLabels, Error, TEXT("Label %s did not have an associated ID"), *ActorLabel.ToString());
			}
		}
	}

	LabeledObjects.Add(Actor, ActorIdPair);

	LabelAllComponents(Actor, ActorIdPair);
}

void UTempoActorLabeler::LabelAllComponents(const AActor* Actor, const FInstanceSemanticIdPair& ActorIdPair)
{
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		LabelComponent(PrimitiveComponent, ActorIdPair);
	}
}

void UTempoActorLabeler::LabelComponent(UActorComponent* Component)
{
	if (!Component->GetOwner() || !UTempoCoreUtils::IsGameWorld(Component))
	{
		return;
	}
	
	if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
	{
		if (const FInstanceSemanticIdPair* ActorIdPair = LabeledObjects.Find(PrimitiveComponent->GetOwner()))
		{
			LabelComponent(PrimitiveComponent, *ActorIdPair);
			return;
		}

		// We've never labeled this component's owner, label the whole Actor instead of just this component.
		LabelActor(PrimitiveComponent->GetOwner());
	}
}

void UTempoActorLabeler::LabelComponent(UPrimitiveComponent* Component, const FInstanceSemanticIdPair& ActorIdPair)
{
	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		if (const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
		{
			const FString MeshFullPath = StaticMesh->GetPathName();
			if (const FName* StaticMeshLabel = StaticMeshLabels.Find(MeshFullPath))
			{
				FInstanceSemanticIdPair IdPair;
				if (LabeledObjects.Contains(Component))
				{
					// This component is already labeled.
					return;
				}
				if (TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
				{
					IdPair.InstanceId = *InstanceId;
				}
				if (const int32* StaticMeshLabelIdPtr = SemanticIds.Find(*StaticMeshLabel))
				{
					IdPair.SemanticId = *StaticMeshLabelIdPtr;
				}
				else
				{
					UE_LOG(LogTempoLabels, Error, TEXT("Label %s did not have an associated ID"), *StaticMeshLabel->ToString());
					return;
				}

				// Label using the explicit static mesh label rather than the owning Actor's label.
				LabeledObjects.Add(Component, IdPair);
				AssignId(Component, IdPair);
				return;
			}
		}
	}

	// No mesh label found. Label with its owning Actor's label.
	LabeledObjects.Add(Component, ActorIdPair);
	AssignId(Component, ActorIdPair);
}

void UTempoActorLabeler::UnLabelAllActors()
{
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		UnLabelActor(*ActorItr);
	}
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

void UTempoActorLabeler::AssignId(UPrimitiveComponent* Component, const FInstanceSemanticIdPair& IdPair)
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
