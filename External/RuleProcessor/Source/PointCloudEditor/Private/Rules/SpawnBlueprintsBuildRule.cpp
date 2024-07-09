// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpawnBlueprintsBuildRule.h"
#include "Styling/SlateStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudEditorSettings.h"
#include "PointCloudView.h"
#include "PointCloudSliceAndDiceExecutionContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor/EditorEngine.h"
#include "AssetSelection.h"
#include "GameFramework/LightWeightInstanceBlueprintFunctionLibrary.h"
#include "ActorFactories/ActorFactory.h"

#define LOCTEXT_NAMESPACE "RuleProcessorSpawnBlueprintRule"

namespace SpawnBlueprintsConstants
{
	const FString Name = FString("Spawn Blueprint");
	const FName TemplateActorName(TEXT("TemplateActor"));
	const FString Description = FString("Spawn a blueprint either using Metadata from the Pointcloud or a named blueprint");
	const FString StatsKey = FString(TEXT("Blueprints"));
}

FSpawnBlueprintsBuildRuleData::FSpawnBlueprintsBuildRuleData()
{
	NamePattern = "$IN_VALUE_$RULEPROCESSOR_ASSET_$METADATA_VALUE_$INDEX";
	MetadataKey = GetDefault<UPointCloudEditorSettings>()->DefaultMetadataKey;

	RegisterOverrideableProperty("NamePattern");
	RegisterOverrideableProperty("MetadataKey");
	RegisterOverrideableProperty("TemplateActor");
	RegisterOverrideableProperty("FolderPath");
}

USpawnBlueprintsBuildRule::USpawnBlueprintsBuildRule()
	: UPointCloudRule(&Data)
{
	Data.TemplateActor = CreateDefaultSubobject<AActor>(SpawnBlueprintsConstants::TemplateActorName);
	Data.TemplateActor->SetFlags(RF_ArchetypeObject);
}
 
FString USpawnBlueprintsBuildRule::Description() const
{
	return SpawnBlueprintsConstants::Description;
}

FString USpawnBlueprintsBuildRule::RuleName() const
{
	return SpawnBlueprintsConstants::Name;
}

FString USpawnBlueprintsBuildRule::MakeName(UPointCloud* Pc, const FString& MetadataValue, const FString& InNamePattern, const FString& InNameValue, int32 Index)
{
	if (Pc == nullptr)
	{
		return FString();
	}

	FString Result = InNamePattern; 

	Result.ReplaceInline(TEXT("$IN_VALUE"), *InNameValue);
	Result.ReplaceInline(TEXT("$RULEPROCESSOR_ASSET"), *Pc->GetName());
	Result.ReplaceInline(TEXT("$MANTLE_ASSET"), *Pc->GetName());
	Result.ReplaceInline(TEXT("$METADATA_VALUE"), *MetadataValue);
	Result.ReplaceInline(TEXT("$INDEX"), *FString::FromInt(Index));

	return Result;
}

void USpawnBlueprintsBuildRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
	Context.ReportObject.AddParameter(TEXT("MetadataKey"), Data.MetadataKey);
	Context.ReportObject.AddParameter(TEXT("FolderPath"), Data.FolderPath.ToString());

	if (Context.ReportObject.GetReportingLevel() >= EPointCloudReportLevel::Properties && Data.OverrideObjectsMap.Num())
	{
		Context.ReportObject.PushFrame(TEXT("Actor Overrides"));
		for (const auto& Element : Data.OverrideObjectsMap)
		{
			FString KeyStaticMesh = TEXT("NULL");
			FString ValueStaticMesh = TEXT("NULL");

			if(!Element.Key.IsNull())
			{
				UObject* KeyObject = Element.Key.LoadSynchronous();
				KeyStaticMesh = KeyObject ? KeyObject->GetName() : TEXT("Invalid object");
			}

			if(!Element.Value.IsNull())
			{
				UObject* ValueObject = Element.Value.LoadSynchronous();
				ValueStaticMesh = ValueObject ? ValueObject->GetName() : TEXT("Invalid object");
			}

			Context.ReportObject.AddMessage(FString::Printf(TEXT("%s->%s"), *KeyStaticMesh, *ValueStaticMesh));
		}
		Context.ReportObject.PopFrame();
	}
}

bool USpawnBlueprintsBuildRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}
	
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		Instance.FinalizeInstance(MakeShareable(new FSpawnBlueprintsBuildRuleInstance(this)));
	}

	return true;
}

FString FSpawnBlueprintsBuildRuleInstance::GetHash()
{
	return GetView()->GetValuesAndTransformsHash({ Data.MetadataKey });
}

bool FSpawnBlueprintsBuildRuleInstance::Execute(FSliceAndDiceExecutionContextPtr Context)
{
	if (Data.World == nullptr)
	{
		return false;
	}

	TArray<FTransform> Transforms;
	TArray<int32> OutIds;
	GetView()->GetTransformsAndIds(Transforms, OutIds);

	if (GenerateReporting())
	{
		// Record statistics for the given view
		ReportFrame->PushParameter(TEXT("Instance count"), Transforms.Num() == OutIds.Num() ? FString::FromInt(Transforms.Num()) : TEXT("Invalid results"));
	}

	if (!GenerateAssets())
	{
		return true;
	}

	if (Transforms.Num() != OutIds.Num())
	{
		return false;
	}

	TArray<AActor*> ActorsCreated;
	TArray<FActorInstanceHandle> ActorHandlesCreated;

	TMap<FString, UClass*> SpawnClassCache;
	TMap< int, FString > MetadataValue = GetView()->GetMetadataValues(Data.MetadataKey);

	// Prepare the target data layers we will push the new actors into
	UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();
	TArray<UDataLayerInstance*> DataLayers;

	if (DataLayerEditorSubsystem && Data.DataLayers.Num() > 0)
	{
		for (const FActorDataLayer& DataLayerInfo : Data.DataLayers)
		{
			if (UDataLayerInstance* DataLayer = DataLayerEditorSubsystem->GetDataLayerInstance(DataLayerInfo.Name))
			{
				DataLayers.Emplace(DataLayer);
			}
		}

		if (DataLayers.Num() != Data.DataLayers.Num())
		{
			UE_LOG(PointCloudLog, Log, TEXT("A target data layer wasn't found for the Spawn Blueprint Rule : %s"), *Rule->Label);
		}
	}

	FScopedSlowTask SlowTask(Transforms.Num(), LOCTEXT("CreatingBlueprints", "Creating Blueprints"));
	SlowTask.MakeDialogDelayed(0.1f);

	bool Result = false;

	for (int i = 0; i < Transforms.Num(); i++)
	{
		SlowTask.EnterProgressFrame();
		int Index = OutIds[i];
		const FTransform& Position = Transforms[i];

		if (MetadataValue.Contains(Index))
		{
			UClass* SpawnClass = nullptr;

			// Get the class of the original Actor type
			FString BpToSpawn = *MetadataValue[Index];

			// look to see if we've already loaded and cached the given BP
			if(SpawnClassCache.Contains(BpToSpawn))
			{
				// if we have, just use the cached version
				SpawnClass = SpawnClassCache[BpToSpawn];
			}
			else
			{
				// Otherwise try and load the class and cast it to a blueprint 
				FSoftObjectPath itemRef = BpToSpawn;
				itemRef.TryLoad();
				UObject* itemObj = itemRef.ResolveObject();

				// Apply override if any
				if (Data.OverrideObjectsMap.Contains(itemObj))
				{
					itemObj = Data.OverrideObjectsMap[itemObj].IsNull() ? nullptr : Data.OverrideObjectsMap[itemObj].LoadSynchronous();
				}

				if (itemObj && Data.bUseLightweightInstancing)
				{
					UActorFactory* ActorFactory = FActorFactoryAssetProxy::GetFactoryForAsset(itemObj);
					SpawnClass = ActorFactory ? ActorFactory->GetDefaultActorClass(itemObj) : nullptr;
				}
				else if(UBlueprint* AsBlueprint = Cast<UBlueprint>(itemObj))
				{
					SpawnClass = AsBlueprint->GeneratedClass;
				}

				SpawnClassCache.Emplace(BpToSpawn, SpawnClass);
			}

			if (SpawnClass == nullptr)
			{
				continue;
			}

			// Step 2. Wherever you need a UClass * reference for that blueprint (like SpawnActor)
			AActor* Actor = nullptr;
			FActorInstanceHandle ActorHandle;

			if (Data.bUseLightweightInstancing)
			{
				ActorHandle = ULightWeightInstanceBlueprintFunctionLibrary::CreateNewLightWeightInstance(SpawnClass, Position, DataLayers.Num() > 0 ? DataLayers[0] : nullptr, Data.World);
			}
			else
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

				if (Data.World == Context->GetWorld())
				{
					SpawnParams.Name = Context->GetActorName(this);
				}

				Actor = Data.World->SpawnActor(SpawnClass, &Position, SpawnParams);
			}

			// Copy properties from template to created actor
			if (Actor && Data.TemplateActor)
			{
				// Unregister components, otherwise we'll have an error popping up
				TArray<UActorComponent*> ComponentsToRegister;
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (Component && Component->IsRegistered())
					{
						Component->UnregisterComponent();
						ComponentsToRegister.Add(Component);
					}
				}

				UEditorEngine::CopyPropertiesForUnrelatedObjects(Data.TemplateActor, Actor);

				// Re-register components we had previously unregistered
				for (UActorComponent* Component : ComponentsToRegister)
				{
					Component->RegisterComponent();
				}
			}

			// Record some statistics
			if (GetStats())
			{
				GetStats()->IncrementCounter(SpawnBlueprintsConstants::StatsKey);
			}

			if (Actor != nullptr)
			{
				FString Name = USpawnBlueprintsBuildRule::MakeName(PointCloud, MetadataValue[Index], Data.NamePattern, Data.NameValue, Index);
				Actor->SetActorLabel(Name);

				if (Data.FolderPath.IsNone() == false)
				{
					Actor->SetFolderPath(Data.FolderPath);
				}

				// Add actor to layers
				if (DataLayerEditorSubsystem && DataLayers.Num() > 0)
				{
					if (!DataLayerEditorSubsystem->AddActorToDataLayers(Actor, DataLayers))
					{
						UE_LOG(PointCloudLog, Log, TEXT("Actor %s was unable to be added to its target data layers"), *Actor->GetActorLabel());
					}
				}

				// This might not be needed, was added as a safe-guard in case there was something wrong in the blueprint
				Actor->ForEachComponent<USceneComponent>(/*bIncludeFromChildActors=*/true, [](USceneComponent* Component) { Component->UpdateBounds(); });

				ActorsCreated.Add(Actor);

				Result = true;
			}
			else if (ActorHandle.IsValid())
			{
				ActorHandlesCreated.Add(ActorHandle);
			}
		}
	}

	if (ActorsCreated.Num() > 0 || ActorHandlesCreated.Num() > 0)
	{
		NewActorsAdded(ActorsCreated, ActorHandlesCreated, GetView());
	}

	return Result;
}

FString FSpawnBlueprintsBuildFactory::Name() const
{
	return SpawnBlueprintsConstants::Name;
}

FString FSpawnBlueprintsBuildFactory::Description() const
{
	return SpawnBlueprintsConstants::Description;
}

UPointCloudRule* FSpawnBlueprintsBuildFactory::Create(UObject *parent)
{
	UPointCloudRule* Result = NewObject<USpawnBlueprintsBuildRule>(parent);	
	return Result;
}

FSpawnBlueprintsBuildFactory::FSpawnBlueprintsBuildFactory(TSharedPtr<ISlateStyle> Style)
{
	FSlateStyleSet* AsStyleSet = static_cast<FSlateStyleSet*>(Style.Get());
	if (AsStyleSet)
	{
		Icon = new FSlateImageBrush(AsStyleSet->RootToContentDir(TEXT("Resources/SingleObjectRule"), TEXT(".png")), FVector2D(128.f, 128.f));
		AsStyleSet->Set("RuleThumbnail.SingleObjectRule", Icon);
	}
	else
	{
		Icon = nullptr;
	}
}

FSpawnBlueprintsBuildFactory::~FSpawnBlueprintsBuildFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FSpawnBlueprintsBuildFactory::GetIcon() const
{ 
	return Icon; 
}

#undef LOCTEXT_NAMESPACE