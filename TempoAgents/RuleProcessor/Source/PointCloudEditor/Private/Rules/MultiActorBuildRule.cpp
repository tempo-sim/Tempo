// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiActorBuildRule.h"
#include "Styling/SlateStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudEditorSettings.h"
#include "PointCloudView.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

namespace MultiActorBuildRules
{
	static const FString Description = "Create multiple actors from the incoming stream by applying a Metadata Filter";
	static const FString Name = "Multi Actor";
	const FName TemplateActorName(TEXT("TemplateActor"));
	const FName TemplateIsmName(TEXT("TemplateISM"));
	const FName TemplateHISMName(TEXT("TemplateHISM"));
	const FName TemplateStaticMeshComponentName(TEXT("TemplateStaticMeshComponent"));
}

FMultiActorBuildRuleData::FMultiActorBuildRuleData()
{
	NamePattern = "$IN_VALUE_$RULEPROCESSOR_ASSET_$METADATAKEY_$METADATAVALUE";	
	MetadataKey = GetDefault<UPointCloudEditorSettings>()->DefaultGroupingMetadataKey;

	RegisterOverrideableProperty("NamePattern");
	RegisterOverrideableProperty("MetadataKey");
	RegisterOverrideableProperty("TemplateActor");
	RegisterOverrideableProperty("TemplateISM");
	RegisterOverrideableProperty("TemplateHISM");
	RegisterOverrideableProperty("TemplateStaticMeshComponent");
	RegisterOverrideableProperty("PerModuleAttributeKey");
	RegisterOverrideableProperty("FolderPath");
	RegisterOverrideableProperty("MaterialOverrides");
}

void FMultiActorBuildRuleData::OverrideNameValue()
{
	FString Name = NamePattern;
	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	NameValue = Name;
}
 
UMultiActorBuildRule::UMultiActorBuildRule()
	: UPointCloudRule(&Data)
{
	// Initialize the templates in the data, done from here because we use a member method
	Data.TemplateActor = CreateDefaultSubobject<AActor>(MultiActorBuildRules::TemplateActorName);
	Data.TemplateActor->SetFlags(RF_ArchetypeObject);
	Data.TemplateISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(MultiActorBuildRules::TemplateIsmName);
	Data.TemplateISM->SetFlags(RF_ArchetypeObject);
	Data.TemplateHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(MultiActorBuildRules::TemplateHISMName);
	Data.TemplateHISM->SetFlags(RF_ArchetypeObject);
	Data.TemplateStaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(MultiActorBuildRules::TemplateStaticMeshComponentName);
	Data.TemplateStaticMeshComponent->SetFlags(RF_ArchetypeObject);
}

FString UMultiActorBuildRule::Description() const
{
	return MultiActorBuildRules::Description;
}

FString UMultiActorBuildRule::RuleName() const
{
	return MultiActorBuildRules::Name;
}

void UMultiActorBuildRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("Key"), Data.MetadataKey);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
	Context.ReportObject.AddParameter(TEXT("FolderPath"), Data.FolderPath.ToString());

	if (Context.ReportObject.GetReportingLevel() >= EPointCloudReportLevel::Properties && Data.ComponentOverrideMap.Num())
	{
		Context.ReportObject.PushFrame(TEXT("Mesh Overrides"));
		for (const auto& Element : Data.ComponentOverrideMap)
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

bool UMultiActorBuildRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}
	
	bool Finalized = false;

	for(FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		check(Instance.GetPointCloud());

		// check that the PointCloud has the given Metadata Key
		if (Instance.GetPointCloud()->HasMetaDataAttribute(Data.MetadataKey) == false)
		{
			UE_LOG(PointCloudLog, Log, TEXT("Point Cloud Does Not Have An Metadata Item %s"), *Data.MetadataKey);
			continue;
		}

		Instance.FinalizeInstance(MakeShareable(new FMultiActorRuleInstance(this)));
		Finalized = true;
	}
	
	return Finalized;
}

FString FMultiActorRuleInstance::GetHash()
{
	TArray<FString> HashKeys = { PointCloudAssetHelpers::GetUnrealAssetMetadataKey() };
	HashKeys.AddUnique(Data.MetadataKey);

	FSpawnAndInitMaterialOverrideParameters MaterialOverrides;
	MaterialOverrides.CopyValid(Data.MaterialOverrides, GetView()->GetPointCloud());

	const TArray<FString> MaterialOverrideMetadataKeys = MaterialOverrides.GetMetadataKeys();

	for (const FString& MaterialOverrideKey : MaterialOverrideMetadataKeys)
	{
		HashKeys.AddUnique(MaterialOverrideKey);
	}

	return GetView()->GetValuesAndTransformsHash(HashKeys);
}

bool FMultiActorRuleInstance::Execute(FSliceAndDiceExecutionContextPtr Context)
{
	UPointCloudView* PointCloudView = GetView();

	// save the stats if we're in the right reporting mode
	if (GenerateReporting())
	{
		// record the statistics for the given view
		int32 ResultCount = GetView()->GetCount();
		ReportFrame->PushParameter(TEXT("Module Count"), FString::FromInt(ResultCount));
	}
	
	Data.OverrideNameValue();

	TMap<FString, FString> ValuesAndLabels = UPointCloudAssetsHelpers::MakeNamesFromMetadataValues(PointCloudView, Data.MetadataKey, Data.NameValue);
	
	if (ValuesAndLabels.Num() == 0)
	{
		UE_LOG(PointCloudLog, Log, TEXT("No names found for Key %s"), *Data.MetadataKey);
		return false;
	}

	if (GenerateReporting())
	{
		ReportFrame->PushParameter(TEXT("Tentative actor count"), FString::FromInt(ValuesAndLabels.Num()));
	}
	
	if (ReportFrame->ReportingLevel > EPointCloudReportLevel::Basic)
	{
		for (const auto& Entry : ValuesAndLabels)
		{
			ReportFrame->AddParameter(Entry.Key, Entry.Value);
		}
	}

	if (!GenerateAssets())
	{
		return true;
	}

	FSpawnAndInitActorParameters Params;
	Params.OverrideMap = Data.ComponentOverrideMap;
	Params.MaterialOverrides.CopyValid(Data.MaterialOverrides, GetView()->GetPointCloud());
	Params.TemplateIsm = Data.TemplateISM;
	Params.TemplateHISM = Data.TemplateHISM;
	Params.TemplateActor = Data.TemplateActor;
	Params.TemplateStaticMeshComponent = Data.TemplateStaticMeshComponent;
	Params.bSingleInstanceAsStaticMesh = Data.bSingleInstanceAsStaticMesh;
	Params.bUseHierarchicalInstancedStaticMeshComponent = Data.bUseHierarchicalInstancedStaticMeshComponent;
	Params.World = Data.World;
	Params.StatsObject = GetStats();
	Params.FolderPath = Data.FolderPath;
	Params.SetNameGetter(Context.Get(), this);

	if (Data.World && Data.World->WorldType == EWorldType::Editor)
	{
		Params.PivotType = Data.PivotType;
		Params.PivotKey = Data.PivotKey;
		Params.PivotValue = Data.PivotValue;
	}

	// if the per module attribute key exists on the PC, add it
	if (PointCloudView->GetPointCloud()->HasMetaDataAttribute(Data.PerModuleAttributeKey))
	{
		Params.PerModuleAttributeKey = Data.PerModuleAttributeKey;
	}

	Params.bManualGroupId = Data.bManualGroupId;
	Params.GroupId = Data.GroupId;

	TMap<FString, FPointCloudManagedActorData> ActorsForThisPC = UPointCloudAssetsHelpers::BulkCreateManagedActorsFromView(PointCloudView, Data.MetadataKey, ValuesAndLabels, Params);

	for (const auto& ManagedActor : ActorsForThisPC)
	{
		const FPointCloudManagedActorData& ActorData = ManagedActor.Value;
		
		if (ActorData.Actor)
		{
			NewActorAdded(ActorData.Actor, ActorData.ActorView);
		}
	}

	return !ActorsForThisPC.IsEmpty();
}

FString FMultiActorBuildFactory::Name() const
{
	return MultiActorBuildRules::Name;
}

FString FMultiActorBuildFactory::Description() const
{
	return MultiActorBuildRules::Description;
}

UPointCloudRule* FMultiActorBuildFactory::Create(UObject* parent)
{
	return NewObject<UMultiActorBuildRule>(parent);
}

FMultiActorBuildFactory::FMultiActorBuildFactory(TSharedPtr<ISlateStyle> Style)
{
	FSlateStyleSet* AsStyleSet = static_cast<FSlateStyleSet*>(Style.Get());
	if (AsStyleSet)
	{
		Icon = new FSlateImageBrush(AsStyleSet->RootToContentDir(TEXT("Resources/MultiObjectRule"), TEXT(".png")), FVector2D(128.f, 128.f));
		AsStyleSet->Set("RuleThumbnail.MultiObjectRule", Icon);
	}
	else
	{
		Icon = nullptr;
	}
}

FMultiActorBuildFactory::~FMultiActorBuildFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FMultiActorBuildFactory::GetIcon() const
{
	return Icon;
}

 