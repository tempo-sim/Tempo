// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsolidateBlueprintISMsToActorBuildRule.h"
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
#include "Subsystems/EditorActorSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Engine/SCS_Node.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "RuleProcessorConsolidateBlueprintISMsToActorBuildRule"

namespace ConsolidateBlueprintISMsToActorConstants
{
	const FString Name = FString("Spawn ConsolidatedISMActor");
	const FString Description = FString("Spawn a Consolidated ISM Actor either using Metadata from the Pointcloud or a named blueprint");
	const FString StatsKey = FString(TEXT("ConsolidatedISMActor"));
}

FConsolidateBlueprintISMsToActorBuildRuleData::FConsolidateBlueprintISMsToActorBuildRuleData()
{
	NamePattern = "$IN_VALUE_$RULEPROCESSOR_ASSET_$METADATA_VALUE_$INDEX";
	BlueprintMetadataKey = GetDefault<UPointCloudEditorSettings>()->DefaultMetadataKey;

	RegisterOverrideableProperty("NamePattern");
	RegisterOverrideableProperty("BlueprintMetadataKey");
}

UConsolidateBlueprintISMsToActorBuildRule::UConsolidateBlueprintISMsToActorBuildRule()
	: UPointCloudRule(&Data)
{
}
 
FString UConsolidateBlueprintISMsToActorBuildRule::Description() const
{
	return ConsolidateBlueprintISMsToActorConstants::Description;
}

FString UConsolidateBlueprintISMsToActorBuildRule::RuleName() const
{
	return ConsolidateBlueprintISMsToActorConstants::Name;
}

FString UConsolidateBlueprintISMsToActorBuildRule::MakeName(UPointCloud* Pc, const FString& MetadataValue, const FString& InNamePattern, const FString& InNameValue, int32 Index)
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

void UConsolidateBlueprintISMsToActorBuildRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
	Context.ReportObject.AddParameter(TEXT("BlueprintMetadataKey"), Data.BlueprintMetadataKey);
	Context.ReportObject.AddParameter(TEXT("ActorMetadataKey"), Data.ActorMetadataKey);

	if (Context.ReportObject.GetReportingLevel() >= EPointCloudReportLevel::Properties && Data.OverrideActorsMap.Num())
	{
		Context.ReportObject.PushFrame(TEXT("Actor Overrides"));
		for (const auto& Element : Data.OverrideActorsMap)
		{
			FString KeyStaticMesh = "NULL";
			FString ValueStaticMesh = "NULL";
			if (Element.Key != nullptr)
			{
				KeyStaticMesh = Element.Key->GetName();
			}

			if (Element.Value != nullptr)
			{
				ValueStaticMesh = Element.Value->GetName();
			}

			Context.ReportObject.AddMessage(FString::Printf(TEXT("%s->%s"), *KeyStaticMesh, *ValueStaticMesh));
		}
		Context.ReportObject.PopFrame();
	}
}

bool UConsolidateBlueprintISMsToActorBuildRule::Compile(FSliceAndDiceContext& Context) const
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
		Instance.FinalizeInstance(MakeShareable(new FConsolidateBlueprintISMsToActorBuildRuleInstance(this)));
	}

	return true;
}

FString FConsolidateBlueprintISMsToActorBuildRuleInstance::GetHash()
{
	return GetView()->GetValuesAndTransformsHash({ Data.ActorMetadataKey });
}

bool FConsolidateBlueprintISMsToActorBuildRuleInstance::Execute(FSliceAndDiceExecutionContextPtr Context)
{
	if (Data.World == nullptr)
	{
		return false;
	}

	if (!GenerateAssets())
	{
		return true;
	}

	TMap<FString, UBlueprint*> BlueprintCache;

	// Get all unique Actors
	TArray< FString > UniqueActorMetadataValue = GetView()->GetUniqueMetadataValues(Data.ActorMetadataKey);
	if (UniqueActorMetadataValue.IsEmpty())
	{
		return false;
	}

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
			UE_LOG(PointCloudLog, Log, TEXT("A target data layer wasn't found for the Consolidate Blueprint ISMs to Actor Rule : %s"), *Rule->Label);
		}
	}

	// Ensure there is a valid EditorActorSubsystem
	UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!EditorActorSubsystem)
	{
		UE_LOG(PointCloudLog, Log, TEXT("A valid EditorActorSubsystem could not be found."));
		return false;
	}

	FScopedSlowTask SlowTask(UniqueActorMetadataValue.Num(), LOCTEXT("CreatingConsolidatedISMActor", "Creating Consolidated ISM Actor"));
	SlowTask.MakeDialogDelayed(0.1f);

	bool Result = false;

	// For each Actor get associated Blueprints
	for (int ActorIndex = 0; ActorIndex < UniqueActorMetadataValue.Num(); ActorIndex++)
	{
		SlowTask.EnterProgressFrame();

		UPointCloudView* BlueprintsPerActorView = GetView()->MakeChildView();
		BlueprintsPerActorView->FilterOnMetadata(Data.ActorMetadataKey, UniqueActorMetadataValue[ActorIndex], EFilterMode::FILTER_Or);

		TMap<int, FString> BlueprintMetadataValue = BlueprintsPerActorView->GetMetadataValues(Data.BlueprintMetadataKey);
		TArray<FTransform> Transforms;
		TArray<int32> OutIds;
		BlueprintsPerActorView->GetTransformsAndIds(Transforms, OutIds);

		if (Transforms.Num() != OutIds.Num())
		{
			continue;
		}

		const FBox Bounds = BlueprintsPerActorView->GetResultsBoundingBox();
		const FVector CenterPivot = Bounds.GetCenter();

		if (AActor* ConsolidatedISMActor = EditorActorSubsystem->SpawnActorFromClass(AActor::StaticClass(), FVector::ZeroVector))
		{
			TArray<UInstancedStaticMeshComponent*> ExistingISMComponents;
				
			ConsolidatedISMActor->SetActorLocation(CenterPivot);
			ConsolidatedISMActor->GetRootComponent()->SetMobility(EComponentMobility::Static);
		
			for (int i = 0; i < Transforms.Num(); i++)
			{
				int Index = OutIds[i];
				const FTransform Position = Transforms[i];

				if (BlueprintMetadataValue.Contains(Index))
				{
					UClass* CurrentBlueprintClass = nullptr;

					// Get the class of the original Actor type
					FString BpToSpawn = *BlueprintMetadataValue[Index];

					// look to see if we've already loaded and cached the given BP
					if (BlueprintCache.Contains(BpToSpawn))
					{
						// if we have, just use the cached version
						CurrentBlueprintClass = BlueprintCache[BpToSpawn]->GeneratedClass;
					}
					else
					{
						// Otherwise try and load the class and cast it to a blueprint 
						FSoftObjectPath itemRef = BpToSpawn;
						itemRef.TryLoad();
						UObject* itemObj = itemRef.ResolveObject();
						UBlueprint* AsBlueprint = Cast<UBlueprint>(itemObj);

						if (AsBlueprint)
						{
							BlueprintCache.Emplace(BpToSpawn, AsBlueprint);
							CurrentBlueprintClass = AsBlueprint->GeneratedClass;
						}
					}

					if (Data.OverrideActorsMap.Contains(CurrentBlueprintClass))
					{
						CurrentBlueprintClass = Data.OverrideActorsMap[CurrentBlueprintClass];
					}

					if (CurrentBlueprintClass == nullptr)
					{
						continue;
					}

					// gather ISM components from BP CDO
					TArray<UInstancedStaticMeshComponent*> FoundISMComponents;
					UBlueprintGeneratedClass* ActorBlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(CurrentBlueprintClass);
					const TArray<USCS_Node*>& ActorBlueprintNodes = ActorBlueprintGeneratedClass->SimpleConstructionScript->GetAllNodes();
					for (USCS_Node* Node : ActorBlueprintNodes)
					{
						if (Node->ComponentClass->IsChildOf(UInstancedStaticMeshComponent::StaticClass()))
						{
							FoundISMComponents.Add(Cast<UInstancedStaticMeshComponent>(Node->ComponentTemplate));
						}
					}

					for (UInstancedStaticMeshComponent* FoundISMComponent : FoundISMComponents)
					{
						// skip if bHiddenInGame is true
						if (FoundISMComponent->bHiddenInGame)
						{
							continue;
						}

						UStaticMesh* FoundISMComponentStaticMesh = FoundISMComponent->GetStaticMesh();
						if (!FoundISMComponentStaticMesh)
						{
							continue;
						}

						TArray<UMaterialInterface*> FoundISMComponentMaterials = FoundISMComponent->GetMaterials();

						UInstancedStaticMeshComponent* ISMComponent = nullptr;
						// check for matching ISM in existing components
						for (UInstancedStaticMeshComponent* ExistingISMComponent : ExistingISMComponents)
						{
							if (FoundISMComponentStaticMesh == ExistingISMComponent->GetStaticMesh())
							{
								if (FoundISMComponentMaterials == ExistingISMComponent->GetMaterials())
								{
									// if match exists set the ISMComponent as the ExistingISMComponent
									ISMComponent = ExistingISMComponent;
								}
							}
						}

						// if no match was found
						if (!ISMComponent)
						{
							// Find unique component name
							int32 Counter = 1;
							FName UniqueComponentName = FoundISMComponentStaticMesh->GetFName();
							while (!FComponentEditorUtils::IsComponentNameAvailable(UniqueComponentName.ToString(), ConsolidatedISMActor))
							{
								UniqueComponentName = FName(*FString::Printf(TEXT("%s_%d"), *UniqueComponentName.ToString(), Counter++));
							}

							// create a new ISM component on ConsolidatedISMActor
							ISMComponent = NewObject<UInstancedStaticMeshComponent>(ConsolidatedISMActor, UInstancedStaticMeshComponent::StaticClass(), UniqueComponentName, RF_Transactional);
							ExistingISMComponents.Add(ISMComponent);
							ISMComponent->SetMobility(EComponentMobility::Static);

							// copy properties
							ISMComponent->SetStaticMesh(FoundISMComponentStaticMesh);
							for (int32 MaterialIndex = 0; MaterialIndex < FoundISMComponentMaterials.Num(); MaterialIndex++)
							{
								ISMComponent->SetMaterial(MaterialIndex, FoundISMComponentMaterials[MaterialIndex]);
							}

							ConsolidatedISMActor->AddInstanceComponent(ISMComponent);
							ConsolidatedISMActor->FinishAddComponent(ISMComponent, false, FTransform::Identity);
						}

						// for each instance in FoundISM, add instance
						TArray<FTransform> FoundInstanceTransforms;
						for (int32 InstanceIndex = 0; InstanceIndex < FoundISMComponent->GetInstanceCount(); InstanceIndex++)
						{
							FTransform FoundInstanceTransform;
							FoundISMComponent->GetInstanceTransform(InstanceIndex, FoundInstanceTransform, false);
							FoundInstanceTransforms.Add(FoundInstanceTransform * Position);
						}
						ISMComponent->AddInstances(FoundInstanceTransforms, /*bShouldReturnIndices*/false, /*bWorldSpace*/true);
					}
				}
			}

			// Destroy Actors with no components
			if (ExistingISMComponents.Num() == 0)
			{
				UE_LOG(PointCloudLog, Log, TEXT("%s contains no components, destroying."), *ConsolidatedISMActor->GetFName().ToString());
				ConsolidatedISMActor->Destroy();
				continue;
			}

			// Swap single instance ISMs to StaticMeshComponents
			TArray<UInstancedStaticMeshComponent*> ComponentsToDestroy;
			for (UInstancedStaticMeshComponent* ExistingISMComponent : ExistingISMComponents)
			{
				if (ExistingISMComponent->GetInstanceCount() == 1)
				{
					UStaticMesh* ExistingISMComponentStaticMesh = ExistingISMComponent->GetStaticMesh();
					TArray<UMaterialInterface*> ExistingISMComponentMaterials = ExistingISMComponent->GetMaterials();
					
					// Find unique component name
					int32 Counter = 1;
					FName UniqueComponentName = ExistingISMComponentStaticMesh->GetFName();
					while (!FComponentEditorUtils::IsComponentNameAvailable(UniqueComponentName.ToString(), ConsolidatedISMActor))
					{
						UniqueComponentName = FName(*FString::Printf(TEXT("%s_%d"), *UniqueComponentName.ToString(), Counter++));
					}

					// create a new StaticMesh component on ConsolidatedISMActor
					UStaticMeshComponent* StaticMeshComponent = NewObject<UStaticMeshComponent>(ConsolidatedISMActor, UStaticMeshComponent::StaticClass(), UniqueComponentName, RF_Transactional);
					StaticMeshComponent->SetMobility(EComponentMobility::Static);

					// copy properties
					StaticMeshComponent->SetStaticMesh(ExistingISMComponentStaticMesh);
					for (int32 MaterialIndex = 0; MaterialIndex < ExistingISMComponentMaterials.Num(); MaterialIndex++)
					{
						StaticMeshComponent->SetMaterial(MaterialIndex, ExistingISMComponentMaterials[MaterialIndex]);
					}

					FTransform InstanceTransform;
					ExistingISMComponent->GetInstanceTransform(0, InstanceTransform);
					ConsolidatedISMActor->AddInstanceComponent(StaticMeshComponent);
					ConsolidatedISMActor->FinishAddComponent(StaticMeshComponent, false, InstanceTransform);

					ComponentsToDestroy.Add(ExistingISMComponent);
				}
			}

			// Destroy remaining ISM with single instance
			for (UInstancedStaticMeshComponent* ComponentToDestroy : ComponentsToDestroy)
			{
				ComponentToDestroy->DestroyComponent();
			}

			// Record some statistics
			if (GetStats())
			{
				GetStats()->IncrementCounter(ConsolidateBlueprintISMsToActorConstants::StatsKey);
			}
				
			if (ConsolidatedISMActor != nullptr)
			{
				FString Name = UConsolidateBlueprintISMsToActorBuildRule::MakeName(PointCloud, UniqueActorMetadataValue[ActorIndex], Data.NamePattern, Data.NameValue, ActorIndex);
				ConsolidatedISMActor->SetActorLabel(Name);

				// Add actor to layers
				if (DataLayerEditorSubsystem && DataLayers.Num() > 0)
				{
					if (!DataLayerEditorSubsystem->AddActorToDataLayers(ConsolidatedISMActor, DataLayers))
					{
						UE_LOG(PointCloudLog, Log, TEXT("Actor %s was unable to be added to its target data layers"), *ConsolidatedISMActor->GetActorLabel());
					}
				}

				NewActorAdded(ConsolidatedISMActor, BlueprintsPerActorView);

				Result = true;
			}
		}
	}
	return Result;
}

FString FConsolidateBlueprintISMsToActorBuildFactory::Name() const
{
	return ConsolidateBlueprintISMsToActorConstants::Name;
}

FString FConsolidateBlueprintISMsToActorBuildFactory::Description() const
{
	return ConsolidateBlueprintISMsToActorConstants::Description;
}

UPointCloudRule* FConsolidateBlueprintISMsToActorBuildFactory::Create(UObject *parent)
{
	UPointCloudRule* Result = NewObject<UConsolidateBlueprintISMsToActorBuildRule>(parent);	
	return Result;
}

FConsolidateBlueprintISMsToActorBuildFactory::FConsolidateBlueprintISMsToActorBuildFactory(TSharedPtr<ISlateStyle> Style)
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

FConsolidateBlueprintISMsToActorBuildFactory::~FConsolidateBlueprintISMsToActorBuildFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FConsolidateBlueprintISMsToActorBuildFactory::GetIcon() const
{ 
	return Icon; 
}

#undef LOCTEXT_NAMESPACE