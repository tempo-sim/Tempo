// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpawnNiagaraRule.h"
#include "Styling/SlateStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudView.h"
#include "Misc/ScopedSlowTask.h"
#include "PointCloudAssetHelpers.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "DataLayer/DataLayerEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "RuleProcessorSpawnBlueprintRule"

namespace SpawnNiagaraConstants
{
	const FString Name = FString("Spawn Niagara System");
	const FName TemplateActorName(TEXT("TemplateActor"));
	const FString Description = FString("Spawn a Niagara System At Each Incoming Point");
	const FString StatsKey = FString("Niagara Component");
	const FString NiagaraSystemIdentifier = TEXT("NiagaraSystem");
}

FSpawnNiagaraRuleData::FSpawnNiagaraRuleData()
{
	NamePattern = "Niagara_$IN_VALUE_$RULEPROCESSOR_ASSET";	

	RegisterOverrideableProperty("NamePattern");	
	RegisterOverrideableProperty("FolderPath");
	RegisterOverrideableProperty("MetadataKey");
}
 
FString USpawnNiagaraRule::Description() const
{
	return SpawnNiagaraConstants::Description;
}

FString USpawnNiagaraRule::RuleName() const
{
	return SpawnNiagaraConstants::Name;
}

const FString FSpawnNiagaraRuleInstance::GetNiagaraSystemIdentifier()
{
	return SpawnNiagaraConstants::NiagaraSystemIdentifier;
}

FString USpawnNiagaraRule::MakeName(UPointCloud* Pc, const FString& InNamePattern, const FString& InNameValue)
{
	if (Pc == nullptr)
	{
		return FString();
	}

	FString Result = InNamePattern; 

	Result.ReplaceInline(TEXT("$IN_VALUE"), *InNameValue);
	Result.ReplaceInline(TEXT("$RULEPROCESSOR_ASSET"), *Pc->GetName());
	Result.ReplaceInline(TEXT("$MANTLE_ASSET"), *Pc->GetName());

	return Result;
}

void USpawnNiagaraRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
	Context.ReportObject.AddParameter(TEXT("FolderPath"), Data.FolderPath.ToString());
	Context.ReportObject.AddParameter(TEXT("MetadataKey"), Data.MetadataKey);

	Context.ReportObject.PushFrame(TEXT("Niagara Systems"));
	for (const UNiagaraSystem *System : Data.NiagaraSystems)
	{
		if (System != nullptr)
		{
			Context.ReportObject.AddParameter(TEXT("Name"), System->GetName());
		}
	}
	Context.ReportObject.PopFrame();	
}

bool USpawnNiagaraRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}

	// if there are no niagara systems selected
	if (Data.NiagaraSystems.Num() == 0 && Data.SpawnMode == ENiagaraSpawnMode::Random)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Niagara Systems Selected"));
		return false;
	}
	
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		Instance.FinalizeInstance(MakeShareable(new FSpawnNiagaraRuleInstance(this)));
	}

	return true;
}

USpawnNiagaraRule::USpawnNiagaraRule()
	: UPointCloudRule(&Data)
{
	Data.TemplateActor = CreateDefaultSubobject<AActor>(SpawnNiagaraConstants::TemplateActorName);
}

FString FSpawnNiagaraRuleInstance::GetHash()
{
	if (Data.SpawnMode == ENiagaraSpawnMode::Data)
	{
		return GetView()->GetValuesAndTransformsHash({ Data.MetadataKey });
	}
	else
	{
		// Default to using the "default" identifying key
		return GetView()->GetValuesAndTransformsHash({ PointCloudAssetHelpers::GetUnrealAssetMetadataKey() });
	}
	
}

bool FSpawnNiagaraRuleInstance::Execute(FSliceAndDiceExecutionContextPtr Context)
{
	if (Data.World == nullptr)
	{
		return false;
	}

	TArray<FTransform> Transforms;
	TMap<int32, FString> MetadataValues;
	TArray<int32> OutIds;
	GetView()->GetTransformsAndIds(Transforms, OutIds);

	if (Data.SpawnMode == ENiagaraSpawnMode::Data)	
	{
		// check that the pointcloud has the given Metadata key
		if (GetView()->GetPointCloud()->HasMetaDataAttribute(Data.MetadataKey) == false)
		{
			UE_LOG(PointCloudLog, Log, TEXT("Point Coud Does Not Have Metadata : %s"), *Data.MetadataKey);
			return false;
		}

		// Get data from the point cloud and find all Results that represent Niagara systems
		TMap<int32, FString> TempMetadataValues;
		TempMetadataValues = GetView()->GetMetadataValues(Data.MetadataKey);
		if (TempMetadataValues.Num() == 0)
		{
			UE_LOG(PointCloudLog, Log, TEXT("Zero Values Returned From GetMetadataValues"));
			return false;
		}

		// Only copy over Niagara system items, this is to avoid trying to load assets that are not Niagara systems
		// It should likely be removed in a future refactor
		for (const auto& Entry : TempMetadataValues)
		{
			if (Entry.Value.StartsWith(GetNiagaraSystemIdentifier()))
			{
				MetadataValues.Add(Entry.Key, Entry.Value);
			}
		}
	}	

	if (GenerateReporting())
	{
		// Record statistics for the given view
		ReportFrame->PushParameter(TEXT("Instance count"), Transforms.Num() == OutIds.Num() ? FString::FromInt(Transforms.Num()) : TEXT("Invalid results"));		
		if (Data.SpawnMode == ENiagaraSpawnMode::Data)
		{
			ReportFrame->PushParameter(TEXT("Metadata Values"), MetadataValues.Num());
		}
		else
		{
			ReportFrame->PushParameter(TEXT("Random Options"), Data.NiagaraSystems.Num());
		}
	}

	if (!GenerateAssets())
	{
		return true;
	}

	if (Transforms.Num() != OutIds.Num())
	{
		return false;
	}

	// If there are no transforms the right thing to do is to return.
	if (Transforms.Num() == 0)
	{
		return true;
	}

	// If in Spawn-From-Data mode and there are no items in the MetadataValues Map, return
	if (MetadataValues.Num() == 0 && Data.SpawnMode == ENiagaraSpawnMode::Data)
	{
		return true;
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
			UE_LOG(PointCloudLog, Log, TEXT("A target data layer wasn't found for the Spawn Blueprint Rule : %s"), *Rule->Label);
		}
	}

	FScopedSlowTask SlowTask(Transforms.Num(), LOCTEXT("CreatingBlueprints", "Creating Blueprints"));
	SlowTask.MakeDialog();

	bool Result = false;

	// work out the name for this actors
	FString Label = USpawnNiagaraRule::MakeName(PointCloud, Data.NamePattern, Data.NameValue);

	// create an actor 
	FSpawnAndInitActorParameters Params;
	Params.TemplateActor = Data.TemplateActor;
	Params.World = Data.World;
	Params.StatsObject = GetStats();
	Params.FolderPath = Data.FolderPath;
	Params.SetNameGetter(Context.Get(), this);

	AActor* AsManaged = UPointCloudAssetsHelpers::GetManagedActor(Label, Params);

	if (AsManaged == nullptr)
	{
		return false;
	}

	USceneComponent* RootComponent = NewObject<USceneComponent>(AsManaged, USceneComponent::GetDefaultSceneRootVariableName(), RF_Transactional);
	RootComponent->Mobility = EComponentMobility::Static;
	AsManaged->SetRootComponent(RootComponent);
	AsManaged->AddInstanceComponent(RootComponent);
	RootComponent->RegisterComponent();
	
	// Record some statistics
	if (GetStats())
	{
		GetStats()->IncrementCounter(TEXT("Root Component"));
	}
	
	if (DataLayerEditorSubsystem && DataLayers.Num() > 0)
	{
		if (!DataLayerEditorSubsystem->AddActorToDataLayers(AsManaged, DataLayers))
		{
			UE_LOG(PointCloudLog, Log, TEXT("Actor %s was unable to be added to its target data layers"), *AsManaged->GetActorLabel());
		}
	}

	TMap<FString, UNiagaraSystem*> SystemCache;

	for (int i = 0; i < Transforms.Num(); i++)
	{
		// get the transform for the Niagara system
		SlowTask.EnterProgressFrame();
		int Index = OutIds[i];
		const FTransform &Position = Transforms[i];

		UNiagaraSystem* SystemToSpawn = nullptr;
		
		switch (Data.SpawnMode)
		{
		case ENiagaraSpawnMode::Random:
		{
			SystemToSpawn = Data.NiagaraSystems[FMath::RandRange(0, Data.NiagaraSystems.Num() - 1)];
		}
		break; 
		case ENiagaraSpawnMode::Data:
		{
			// If the transform also has an entry in the MetadataValues map
			if (MetadataValues.Contains(Index) == true)
			{
				// Try and find the System in the cache
				const FString& SystemName = *MetadataValues.Find(Index);
				UNiagaraSystem** CacheEntry = SystemCache.Find(SystemName);
				if (CacheEntry)
				{
					// if we found it in the cache, use that value
					SystemToSpawn = *CacheEntry;
				}
				else
				{
					// otherwise try and load the Niagara system
					FSoftObjectPath itemRef = SystemName;
					itemRef.TryLoad();
					UObject* itemObj = itemRef.ResolveObject();
					SystemToSpawn = Cast<UNiagaraSystem>(itemObj);

					if (SystemToSpawn)
					{
						// if we were able to load the system, add it to the cache
						SystemCache.Add(SystemName, SystemToSpawn);
					}					
				}
			}
			else
			{
				// This transform does not corrispond to a Niagara system
				continue;
			}
		}		
		break; 
		}

		if (!SystemToSpawn)
		{
			continue;
		}

		// Spawn it
		UNiagaraComponent* NiagaraComponent = NewObject<UNiagaraComponent>(AsManaged);
		NiagaraComponent->SetAsset(SystemToSpawn);
		
		// Record some statistics
		if (GetStats())
		{
			GetStats()->IncrementCounter(SpawnNiagaraConstants::StatsKey);
		}

		NiagaraComponent->SetMobility(AsManaged->GetRootComponent()->Mobility);
		NiagaraComponent->SetWorldTransform(Position);
		NiagaraComponent->AttachToComponent(AsManaged->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

		NiagaraComponent->RegisterComponent();
		AsManaged->AddInstanceComponent(NiagaraComponent);

		Result = true;
	}

	NewActorAdded(AsManaged, GetView());

	return Result;
}

FString FSpawnNiagaraFactory::Name() const
{
	return SpawnNiagaraConstants::Name;
}

FString FSpawnNiagaraFactory::Description() const
{
	return SpawnNiagaraConstants::Description;
}

UPointCloudRule* FSpawnNiagaraFactory::Create(UObject *parent)
{
	UPointCloudRule* Result = NewObject<USpawnNiagaraRule>(parent);	
	return Result;
}

FSpawnNiagaraFactory::FSpawnNiagaraFactory(TSharedPtr<ISlateStyle> Style)
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

FSpawnNiagaraFactory::~FSpawnNiagaraFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FSpawnNiagaraFactory::GetIcon() const
{ 
	return Icon; 
}

#undef LOCTEXT_NAMESPACE
