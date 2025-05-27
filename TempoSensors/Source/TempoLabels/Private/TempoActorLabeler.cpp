// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorLabeler.h"

#include "TempoLabels.h"
#include "TempoLabelTypes.h"
#include "TempoInstancedStaticMeshComponent.h"

#include "TempoSensorsSettings.h"

#include "TempoCoreUtils.h"
#include "DefaultActorClassifier.h"

#include "Engine.h"

FInstanceIdAllocator::FInstanceIdAllocator(int32 MinIdIn, int32 MaxIdIn)
	: MinId(MinIdIn), MaxId(MaxIdIn)
{
	for (int32 I = MinIdIn; I <= MaxIdIn; ++I)
	{
		AvailableIds.Add(I);
	}
}

TOptional<int32> FInstanceIdAllocator::Allocate()
{
	if (auto AvailableIdIt = AvailableIds.CreateIterator())
	{
		const int32 AvailableId = *AvailableIdIt;
		AvailableIdIt.RemoveCurrent();
		return AvailableId;
	}
	return TOptional<int32>();
}

void FInstanceIdAllocator::Reclaim(int32 Id)
{
 	if (!ensureMsgf(Id >= MinId && Id <= MaxId, TEXT("Reclaimed Available Id %d outside original min/max"), Id))
 	{
 		return;
 	}
 	AvailableIds.Add(Id);
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

bool UTempoActorLabeler::ShouldCreateSubsystem(UObject* Outer) const
{
	if (UTempoCoreUtils::IsGameWorld(Outer))
	{
		return Super::ShouldCreateSubsystem(Outer);
	}
	return false;
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
				if (ActorLabels.Contains(ActorType))
				{
					UE_LOG(LogTempoLabels, Error, TEXT("Actor type %s is associated with more than one label (%s and %s)"), *ActorType->GetName(), *ActorLabels[ActorType].ToString(), *Label.ToString());
					continue;
				}
				ActorLabels.Add(ActorType, Label);
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
		if (LabelIds.Contains(Label))
		{
			UE_LOG(LogTempoLabels, Error, TEXT("Label name %s is associated with more than one label ID (%d and %d)"), *Label.ToString(), LabelIds[Label], LabelId);
		}
		else
		{
			LabelIds.Add(Label, LabelId);
		}
	});
	
	if (const int32* NoLabelIdPtr = LabelIds.Find(NoLabelName))
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
	if (const int32* ActorLabelId = LabeledActors.Find(Actor))
	{
		// We've labeled this Actor before. Make sure all the components are labeled.
		LabelAllComponents(Actor, *ActorLabelId);
		return;
	}

	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoLabels, Error, TEXT("Semantic Label table was not set"));
		return;
	}

	int32 ActorLabelId = NoLabelId;
	FName AssignedLabel = NoLabelName;
	for (const auto& Elem : ActorLabels)
	{
		const TSubclassOf<AActor>& ActorType = Elem.Key;
		const FName& ActorLabel = Elem.Value;
		if (Actor->GetClass()->IsChildOf(ActorType.Get()))
		{
			if (const int32* LabelId = LabelIds.Find(ActorLabel))
			{
				if (AssignedLabel != NoLabelName && *ActorLabel.ToString() != AssignedLabel)
				{
					UE_LOG(LogTempoLabels, Error, TEXT("Labels %s and %s have overlapping actor types"), *ActorLabel.ToString(), *AssignedLabel.ToString());
					continue;
				}
				AssignedLabel = ActorLabel;
				if (GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance)
				{
					if (TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
					{
						ActorLabelId = *InstanceId;
					}
					else
					{
						UE_LOG(LogTempoLabels, Error, TEXT("Unable to allocate instance while labeling %s"), *Actor->GetActorNameOrLabel());
						return;
					}
				}
				else
				{
					ActorLabelId = *LabelId;
				}
			}
			else
			{
				UE_LOG(LogTempoLabels, Error, TEXT("Label %s did not have an associated ID"), *ActorLabel.ToString());
			}
		}
	}

	LabeledActors.Add(Actor, ActorLabelId);

	LabelAllComponents(Actor, ActorLabelId);
}

void UTempoActorLabeler::LabelAllComponents(const AActor* Actor, int32 ActorLabelId)
{
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		LabelComponent(PrimitiveComponent, ActorLabelId);
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
		if (const int32* ActorLabelId = LabeledActors.Find(PrimitiveComponent->GetOwner()))
		{
			LabelComponent(PrimitiveComponent, *ActorLabelId);
			return;
		}

		// We've never labeled this component's owner, label the whole Actor instead of just this component.
		LabelActor(PrimitiveComponent->GetOwner());
	}
}

void UTempoActorLabeler::LabelComponent(UPrimitiveComponent* Component, int32 ActorLabelId)
{
	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		if (const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
		{
			const FString MeshFullPath = StaticMesh->GetPathName();
			if (const FName* StaticMeshLabel = StaticMeshLabels.Find(MeshFullPath))
			{
				int32 StaticMeshLabelId = NoLabelId;
				if (GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance)
				{
					if (LabeledComponents.Contains(Component))
					{
						// This component is already labeled.
						return;
					}
					if (TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
					{
						StaticMeshLabelId = *InstanceId;
					}
					else
					{
						UE_LOG(LogTempoLabels, Error, TEXT("Unable to allocate instance while labeling %s"), *Component->GetName());
						return;
					}
				}
				else
				{
					if (const int32* StaticMeshLabelIdPtr = LabelIds.Find(*StaticMeshLabel))
					{
						StaticMeshLabelId = *StaticMeshLabelIdPtr;
					}
					UE_LOG(LogTempoLabels, Error, TEXT("Label %s did not have an associated ID"), *StaticMeshLabel->ToString());
				}
				if (const int32* PreviousLabel = LabeledComponents.Find(Component))
				{
					if (StaticMeshComponent->bRenderCustomDepth && *PreviousLabel == StaticMeshLabelId)
					{
						// This component already has the right label.
						return;
					}
				}

				// Label using the explicit static mesh label rather than the owning Actor's label.
				LabeledComponents.Add(Component, StaticMeshLabelId);
				StaticMeshComponent->SetRenderCustomDepth(true);
				StaticMeshComponent->SetCustomDepthStencilValue(StaticMeshLabelId);
				return;
			}
		}
	}

	// No mesh label found. Label with its owning Actor's label.
	LabeledComponents.Add(Component, ActorLabelId);
	Component->SetRenderCustomDepth(true);
	Component->SetCustomDepthStencilValue(ActorLabelId);
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
	if (!LabeledActors.Contains(Actor))
	{
		// We've never labeled this Actor.
		return;
	}

	UnLabelAllComponents(Actor);

	if (GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance)
	{
		if (const int32* ActorLabelId = LabeledActors.Find(Actor); *ActorLabelId != NoLabelId)
		{
			InstanceIdAllocator.Reclaim(*ActorLabelId);
		}
	}

	LabeledActors.Remove(Actor);
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
		if (const int32* ComponentLabelId = LabeledComponents.Find(Component))
		{
			const int32* ActorLabelId = LabeledActors.Find(Component->GetOwner());
			if (!ActorLabelId || (*ActorLabelId != *ComponentLabelId && *ComponentLabelId != NoLabelId))
			{
				InstanceIdAllocator.Reclaim(*ComponentLabelId);
			}
		}
	}

	LabeledComponents.Remove(Component);
}

void UTempoActorLabeler::ReLabelAllActors()
{
	UnLabelAllActors();
	LabelAllActors();
}

FName UTempoActorLabeler::GetActorClassification(const AActor* Actor) const
{
	for (const auto& Elem : ActorLabels)
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
