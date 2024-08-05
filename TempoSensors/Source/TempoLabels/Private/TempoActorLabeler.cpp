// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorLabeler.h"

#include "TempoLabels.h"
#include "TempoLabelTypes.h"

#include "TempoSensorsSettings.h"

#include "Engine.h"

void UTempoActorLabeler::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Only for game worlds
	if (!(InWorld.WorldType == EWorldType::Game || InWorld.WorldType == EWorldType::PIE))
	{
		return;
	}

	SemanticLabelTable = GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable();

	// Parse the semantic label table into a more convenient structure.
	BuildLabelMaps();

	// Label all actors *after* BeginPlay (UWorldSubsystem::OnWorldBeginPlay is called *before* BeginPlay).
	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoActorLabeler::LabelAllActors);

	// Label all newly spawned actors.
	GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UTempoActorLabeler::LabelActor));

	// Handles labeling any component whose render state is marked dirty (for example their mesh changed).
	UActorComponent::MarkRenderStateDirtyEvent.AddWeakLambda(this, [this](UActorComponent& Component)
	{
		LabelComponent(&Component);
	});

	// Handles labeling any component who is created after their Actor is spawned. 
	UActorComponent::GlobalCreatePhysicsDelegate.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		LabelComponent(Component);
	});
}

void UTempoActorLabeler::BuildLabelMaps()
{
	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoLabels, Error, TEXT("Semantic label table was not set"));
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
	}

	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoLabels, Error, TEXT("Semantic label table was not set"));
		return;
	}

	FName AssignedLabel = NoLabelName;
	int32 ActorLabelId = NoLabelId;
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
				ActorLabelId = *LabelId;
			}
			else
			{
				UE_LOG(LogTempoLabels, Error, TEXT("Label %s did not have an associated ID"), *ActorLabel.ToString());
			}
		}
	}
	
	LabelAllComponents(Actor, ActorLabelId);

	LabeledActors.Add(Actor, ActorLabelId);
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
	if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
	{
		if (const int32* ActorLabelId = LabeledActors.Find(PrimitiveComponent->GetOwner()))
		{
			LabelComponent(PrimitiveComponent, *ActorLabelId);
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
				if (const int32* StaticMeshLabelId = LabelIds.Find(*StaticMeshLabel))
				{
					if (const int32* PreviousLabel = LabeledComponents.Find(Component))
					{
						if (StaticMeshComponent->bRenderCustomDepth && StaticMeshComponent->CustomDepthStencilValue == *PreviousLabel)
						{
							// This component already has the right label.
							return;
						}
					}

					// Label using the explicit static mesh label rather than the owning Actor's label.
					StaticMeshComponent->SetRenderCustomDepth(true);
					StaticMeshComponent->SetCustomDepthStencilValue(*StaticMeshLabelId);
					LabeledComponents.Add(StaticMeshComponent, *StaticMeshLabelId);
					return;
				}
				UE_LOG(LogTempoLabels, Error, TEXT("Label %s did not have an associated ID"), *StaticMeshLabel->ToString());
			}
		}
	}

	// No mesh label found. Label with its owning Actor's label.
	Component->SetRenderCustomDepth(true);
	Component->SetCustomDepthStencilValue(ActorLabelId);
	LabeledComponents.Add(Component, ActorLabelId);
}
