// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExecuteBlueprintRule.h"
#include "PointCloudView.h"
#include "PointCloudSliceAndDiceExecutionContext.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ExecuteBlueprintRule"

namespace ExecuteBlueprintConstants
{
	static const FString Description = LOCTEXT("Description", "Execute the specified blueprint").ToString();
	static const FString Name = LOCTEXT("Name", "Execute Blueprint").ToString();
}

FExecuteBlueprintRuleData::FExecuteBlueprintRuleData()
{
}

UExecuteBlueprintRule::UExecuteBlueprintRule()
{
	InitSlots(1);
}

FString UExecuteBlueprintRule::Description() const
{
	return ExecuteBlueprintConstants::Description;
}

FString UExecuteBlueprintRule::RuleName() const
{
	return ExecuteBlueprintConstants::Name;
}

void UExecuteBlueprintRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);

	Context.ReportObject.AddParameter(TEXT("PointCloudBlueprint"), Data.ExecuteBlueprint ? Data.ExecuteBlueprint->GetPathName() : TEXT("None"));
}

FString UExecuteBlueprintRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	switch (SlotIndex)
	{
	case BLUEPRINT_SLOT:
		return FString(TEXT("Output"));
		break;
	default:
		return FString(TEXT("Unknown"));
	}
}

bool UExecuteBlueprintRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}

	UClass* ObjectClass = nullptr;
	if (UPointCloudBlueprint* Blueprint = Cast<UPointCloudBlueprint>(Data.ExecuteBlueprint))
	{
		ObjectClass = Blueprint->GeneratedClass;
	}

	if (!ObjectClass)
	{
		return false;
	}
	
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FExecuteBlueprintRuleInstance(this, ObjectClass));

		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			Instance.EmitInstance(RuleInstance, GetSlotName(0));
			Slot->Compile(Context);
			Instance.ConsumeInstance(RuleInstance);
		}
		else
		{
			Instance.FinalizeInstance(RuleInstance);
		}
	}

	return true;
}

bool FExecuteBlueprintRuleInstance::Execute(FSliceAndDiceExecutionContextPtr Context)
{
	if (!PointCloudBlueprintObject)
	{
		return false;
	}

	if (!GenerateAssets())
	{
		return true;
	}

	UPointCloudBlueprintObject* PointCloudBlueprintObjectInstance = NewObject<UPointCloudBlueprintObject>(GetTransientPackage(), PointCloudBlueprintObject);
	if (!PointCloudBlueprintObjectInstance)
	{
		return false;
	}
	PointCloudBlueprintObjectInstance->AddToRoot();

	if (UWorld* World = Context->GetWorld())
	{
		FOnActorSpawned::FDelegate ActorSpawnedDelegate = FOnActorSpawned::FDelegate::CreateLambda([this](AActor* InActor)
			{
				SpawnedActors.Emplace(InActor);
			});
		OnActorSpawnedDelegateHandle = World->AddOnActorSpawnedHandler(ActorSpawnedDelegate);
	}
	
	UPointCloudView* CurrentView = GetView();
	UWorld* CurrentWorld = GetWorld();
	PointCloudBlueprintObjectInstance->SetWorld(CurrentWorld);

	FEditorScriptExecutionGuard ScriptGuard;
	PointCloudBlueprintObjectInstance->Execute(CurrentView);

	GetView()->PreCacheFilters();

	PointCloudBlueprintObjectInstance->RemoveFromRoot();
	return true;
}

bool FExecuteBlueprintRuleInstance::PostExecute(FSliceAndDiceExecutionContextPtr Context)
{
	if (UWorld* World = Context->GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(OnActorSpawnedDelegateHandle);
	}
	OnActorSpawnedDelegateHandle.Reset();

	//Blueprint generated content can depend on external factors so hash based skipping is not supported.
	NewActorsAdded(SpawnedActors, GetView());
	SpawnedActors.Empty();

	return Super::PostExecute(Context);
}

FString FExecuteBlueprintFactory::Description() const
{
	return ExecuteBlueprintConstants::Description;
}

FString FExecuteBlueprintFactory::Name() const
{
	return ExecuteBlueprintConstants::Name;
}

UPointCloudRule* FExecuteBlueprintFactory::Create(UObject* Parent)
{
	return NewObject<UExecuteBlueprintRule>(Parent);
}

#undef LOCTEXT_NAMESPACE
