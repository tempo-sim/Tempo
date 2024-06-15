// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpawnPackedBlueprintsBuildRule.h"

#include "Brushes/SlateImageBrush.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudView.h"
#include "PointCloudSliceAndDiceExecutionContext.h"
#include "Styling/SlateStyle.h"
#include "Engine/World.h"
#include "LevelInstance/LevelInstanceSubsystem.h"

#define LOCTEXT_NAMESPACE "RuleProcessorSpawnPackedBlueprintRule"

namespace SpawnPackedBlueprintsConstants
{
	const FString Name = LOCTEXT("Name", "Spawn a Packed Level Instance Blueprint").ToString();
	const FString Description = LOCTEXT("Description", "Create a packed level instance blueprint either using Metadata from the Pointcloud or a named blueprint, and spawn an actor for it").ToString();
}

FSpawnPackedBlueprintsBuildRuleData::FSpawnPackedBlueprintsBuildRuleData()
{
	RegisterOverrideableProperty(TEXT("ContentFolder"));
	RegisterOverrideableProperty(TEXT("PivotType"));

	ContentFolder.Path = TEXT("/");
}

USpawnPackedBlueprintsBuildRule::USpawnPackedBlueprintsBuildRule()
{	
	InitSlots(1);
}

FString USpawnPackedBlueprintsBuildRule::Description() const
{
	return SpawnPackedBlueprintsConstants::Description;
}


FString USpawnPackedBlueprintsBuildRule::RuleName() const
{
	return SpawnPackedBlueprintsConstants::Name;
}

FString USpawnPackedBlueprintsBuildRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	switch (SlotIndex)
	{
	case SUBLEVEL_SLOT:
		return FString(TEXT("Inside Level Instance"));
		break;
	default:
		return FString(TEXT("Unknown"));
	}
}
 
bool USpawnPackedBlueprintsBuildRule::Compile(FSliceAndDiceContext& Context) const
{	
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}
	
	bool Result = false;
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FSpawnPackedBlueprintsBuildRuleInstance(this));
			Instance.EmitInstance(RuleInstance, GetSlotName(0));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop instance
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return Result;
}

bool FSpawnPackedBlueprintsBuildRuleInstance::PostExecuteInternal(FSliceAndDiceExecutionContextPtr Context)
{
	if (!GenerateAssets())
	{
		return true;
	}

	// Force context to dump changes to make sure that the packed level instance process works
	Context->ForceDumpChanges();

	// we will use a subsystem to do the heavy lifting of asset creation/management
	ULevelInstanceSubsystem* LevelInstanceSubsystem = GetWorld()->GetSubsystem<ULevelInstanceSubsystem>();
	if (LevelInstanceSubsystem == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Could not find LevelInstanceSubsystem, no packed level instance blueprint was generated."));
		return false;
	}

	FNewLevelInstanceParams LevelInstanceParams;
	LevelInstanceParams.Type = Data.LevelInstanceType == EPointCloudLevelInstanceType::LevelInstance ? ELevelInstanceCreationType::LevelInstance : ELevelInstanceCreationType::PackedLevelActor;
	LevelInstanceParams.PivotType = Data.PivotType;
	LevelInstanceParams.LevelPackageName = Data.ContentFolder.Path / Data.NameValue;
	LevelInstanceParams.SetExternalActors(Data.bExternalActors);

	TArray<FSliceAndDiceActorMapping> AllSoftGeneratedActorMappings = ReturnAndClearGeneratedActors();
	TArray<TSoftObjectPtr<AActor>> SoftGeneratedActors = SliceAndDiceManagedActorsHelpers::ToActorList(AllSoftGeneratedActorMappings);

	TArray<AActor*> GeneratedActors;
	for (TSoftObjectPtr<AActor> Actor : SoftGeneratedActors)
	{
		if (AActor* LoadedActor = Actor.LoadSynchronous())
		{
			GeneratedActors.Add(LoadedActor);
		}
	}

	ALevelInstance* LevelInstance = nullptr;

	if (GeneratedActors.Num() > 0)
	{
		if (LevelInstanceParams.PivotType == ELevelInstancePivotType::Actor)
		{
			LevelInstanceParams.PivotActor = GeneratedActors[0];
		}

		LevelInstance = Cast<ALevelInstance>(LevelInstanceSubsystem->CreateLevelInstanceFrom(GeneratedActors, LevelInstanceParams));
	}

	if (LevelInstance == nullptr)
	{
		UE_LOG(PointCloudLog, Error, TEXT("Level Instance was not created"));
		return false;
	}

	if (LevelInstance->GetWorld() == nullptr)
	{
		UE_LOG(PointCloudLog, Error, TEXT("Level Instance world is not spawned in the level"));
		return false;
	}

	NewActorAdded(LevelInstance, GetView());
	return true;
}

void USpawnPackedBlueprintsBuildRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("Content Folder"), Data.ContentFolder.Path);

	switch (Data.LevelInstanceType)
	{
		case EPointCloudLevelInstanceType::LevelInstance:
		{
			Context.ReportObject.AddParameter(TEXT("Actor Type"), TEXT("Level Instance"));
			break;
		}
		case EPointCloudLevelInstanceType::PackedLevelInstance:
		{
			Context.ReportObject.AddParameter(TEXT("Actor Type"), TEXT("Packed Level Instance"));
			break;
		}
		case EPointCloudLevelInstanceType::PackedLevelInstanceBlueprint: // fallthrough to default!
		default:
		{
			Context.ReportObject.AddParameter(TEXT("Actor Type"), TEXT("Packed Level Instance Blueprint"));
			break;
		}
	} // end of switch (Data.LevelInstanceType)

	Context.ReportObject.AddParameter(TEXT("Pivot Type"), UEnum::GetValueAsString(Data.PivotType));
	Context.ReportObject.AddParameter(TEXT("External Actor"), Data.bExternalActors);
}

FString FSpawnPackedBlueprintsBuildFactory::Name() const
{
	return SpawnPackedBlueprintsConstants::Name;
}

FString FSpawnPackedBlueprintsBuildFactory::Description() const
{
	return SpawnPackedBlueprintsConstants::Description;
}

UPointCloudRule* FSpawnPackedBlueprintsBuildFactory::Create(UObject *parent)
{
	return NewObject<USpawnPackedBlueprintsBuildRule>(parent);	
}

FSpawnPackedBlueprintsBuildFactory::FSpawnPackedBlueprintsBuildFactory(TSharedPtr<ISlateStyle> Style)
{
	FSlateStyleSet* AsStyleSet = static_cast<FSlateStyleSet*>(Style.Get());
	if (AsStyleSet)
	{
		Icon = new FSlateImageBrush(AsStyleSet->RootToContentDir(TEXT("Resources/SingleObjectRule"), TEXT(".png")), FVector2D(128.f, 128.f));
		AsStyleSet->Set("RuleThumbnail.MultiObjectRule", Icon);
	}
	else
	{
		Icon = nullptr;
	}
}

FSpawnPackedBlueprintsBuildFactory::~FSpawnPackedBlueprintsBuildFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FSpawnPackedBlueprintsBuildFactory::GetIcon() const
{ 
	return Icon; 
}

#undef LOCTEXT_NAMESPACE