// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorLabeler.h"

#include "TempoLabels.h"
#include "TempoLabelTypes.h"

#include "Engine.h"

#include "TempoSensorsSettings.h"

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
			if (ActorLabels.Contains(ActorType))
			{
				UE_LOG(LogTempoLabels, Error, TEXT("Actor type %s is associated with more than one label (%s and %s)"), *ActorType->GetName(), *ActorLabels[ActorType].ToString(), *Label.ToString());
				continue;
			}
			ActorLabels.Add(ActorType, Label);
		}

		for (const TSoftObjectPtr<UStaticMesh>& StaticMeshAsset : Value.StaticMeshTypes)
		{
			FString MeshId;
			StaticMeshAsset.LoadSynchronous()->GetMeshId(MeshId);
			if (StaticMeshLabels.Contains(MeshId))
			{
				UE_LOG(LogTempoLabels, Error, TEXT("Static mesh type %s is associated with more than one label (%s and %s)"), *MeshId, *StaticMeshLabels[MeshId].ToString(), *Label.ToString());
				continue;
			}
			StaticMeshLabels.Add(MeshId, Label);
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
}

void UTempoActorLabeler::LabelAllActors() const
{
	UE_LOG(LogTempoLabels, Display, TEXT("Labeling all actors in world"));
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		LabelActor(*ActorItr);
	}
}

void UTempoActorLabeler::LabelActor(AActor* Actor) const
{
	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoLabels, Error, TEXT("Semantic label table was not set"));
		return;
	}

	FName AssignedLabel = NAME_None;
	int32 ActorLabelId = 0;
	for (const auto& Elem : ActorLabels)
	{
		const TSubclassOf<AActor>& ActorType = Elem.Key;
		const FName& ActorLabel = Elem.Value;
		if (Actor->GetClass()->IsChildOf(ActorType.Get()))
		{
			if (const int32* LabelId = LabelIds.Find(ActorLabel))
			{
				if (AssignedLabel != NAME_None)
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

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
		{
			if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				FString MeshId;
				StaticMesh->GetMeshId(MeshId);
				if (const FName* StaticMeshLabel = StaticMeshLabels.Find(MeshId))
				{
					if (const int32* StaticMeshLabelId = LabelIds.Find(*StaticMeshLabel))
					{
						PrimitiveComponent->SetRenderCustomDepth(true);
						PrimitiveComponent->SetCustomDepthStencilValue(*StaticMeshLabelId);
					}
					else
					{
						UE_LOG(LogTempoLabels, Error, TEXT("Label %s did not have an associated ID"), *StaticMeshLabel->ToString());
					}
					// Explicit meshes for static mesh labels take precedence over their owning actor's label.
					continue;
				}
			}
		}

		if (AssignedLabel != NAME_None)
		{
			PrimitiveComponent->SetRenderCustomDepth(true);
			PrimitiveComponent->SetCustomDepthStencilValue(ActorLabelId);
		}
	}
}
