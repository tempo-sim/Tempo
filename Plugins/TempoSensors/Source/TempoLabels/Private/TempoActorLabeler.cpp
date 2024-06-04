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

	// Label all actors *after* BeginPlay (UWorldSubsystem::OnWorldBeginPlay is called *before* BeginPlay).
	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoActorLabeler::LabelAllActors);
	
	// Label all newly spawned actors.
	GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UTempoActorLabeler::LabelActor));
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
	
	FName AssignedLabelName = NAME_None;
	SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""), [&AssignedLabelName, Actor](const FName& Key, const FSemanticLabel& Value)
	{
		const FName& LabelName = Key;
		for (const TSubclassOf<AActor>& ActorType : Value.ActorTypes)
		{
			if (Actor->GetClass()->IsChildOf(ActorType.Get()))
			{
				if (AssignedLabelName != NAME_None)
				{
					UE_LOG(LogTempoLabels, Error, TEXT("Labels %s and %s have overlapping actor types"), *LabelName.ToString(), *AssignedLabelName.ToString());
					return;
				}
				AssignedLabelName = LabelName;
			
				TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
				for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
				{
					PrimitiveComponent->SetRenderCustomDepth(true);
					PrimitiveComponent->SetCustomDepthStencilValue(Value.Label);
				}
			}
		}
	});
}
