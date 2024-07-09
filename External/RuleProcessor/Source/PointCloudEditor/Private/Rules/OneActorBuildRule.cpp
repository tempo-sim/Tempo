// Copyright Epic Games, Inc. All Rights Reserved.

#include "OneActorBuildRule.h"
#include "Styling/SlateStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudView.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PointCloudSliceAndDiceExecutionContext.h"

namespace OneActorBuildRuleConstants
{
	const FName TemplateActorName(TEXT("TemplateActor"));
	const FName TemplateIsmName(TEXT("TemplateISM"));
	const FName TemplateHISMName(TEXT("TemplateHISM"));
	const FString Description(TEXT("Create a single actor and populate it with instances from the given point cloud"));
	const FString Name(TEXT("One Actor"));
	const FName TemplateStaticMeshComponentName(TEXT("TemplateStaticMeshComponent"));
}

FOneActorBuildRuleData::FOneActorBuildRuleData()
{
	RegisterOverrideableProperty("NamePattern");
	RegisterOverrideableProperty("TemplateActor");
	RegisterOverrideableProperty("TemplateISM");
	RegisterOverrideableProperty("TemplateHISM");
	RegisterOverrideableProperty("TemplateStaticMeshComponent");
	RegisterOverrideableProperty("PerModuleAttributeKey");
	RegisterOverrideableProperty("FolderPath");
	RegisterOverrideableProperty("MaterialOverrides");

	NamePattern = TEXT("$IN_VALUE_$RULEPROCESSOR_ASSET");
}
 
UOneActorBuildRule::UOneActorBuildRule()
	: UPointCloudRule(&Data)
{	
	// Finally, setup template actor & ism in the data - done here because CreateDefaultSubobject is a member call
	Data.TemplateActor = CreateDefaultSubobject<AActor>(OneActorBuildRuleConstants::TemplateActorName);
	Data.TemplateActor->SetFlags(RF_ArchetypeObject);
	Data.TemplateISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(OneActorBuildRuleConstants::TemplateIsmName);
	Data.TemplateISM->SetFlags(RF_ArchetypeObject);
	Data.TemplateHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(OneActorBuildRuleConstants::TemplateHISMName);
	Data.TemplateHISM->SetFlags(RF_ArchetypeObject);
	Data.TemplateStaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(OneActorBuildRuleConstants::TemplateStaticMeshComponentName);
	Data.TemplateStaticMeshComponent->SetFlags(RF_ArchetypeObject);
}

FString UOneActorBuildRule::Description() const
{
	return OneActorBuildRuleConstants::Description;
}

FString UOneActorBuildRule::RuleName() const
{
	return OneActorBuildRuleConstants::Name;
}

FString UOneActorBuildRule::MakeName(UPointCloud* Pc, const FString& InNamePattern, const FString& InNameValue)
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

void UOneActorBuildRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);

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


bool UOneActorBuildRule::Compile(FSliceAndDiceContext& Context) const
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
		Instance.FinalizeInstance(MakeShareable(new FOneActorRuleInstance(this)));
	}

	return true;
}

FString FOneActorRuleInstance::GetHash()
{
	TArray<FString> HashKeys = { PointCloudAssetHelpers::GetUnrealAssetMetadataKey() };

	FSpawnAndInitMaterialOverrideParameters MaterialOverrides;
	MaterialOverrides.CopyValid(Data.MaterialOverrides, GetView()->GetPointCloud());

	const TArray<FString> MaterialOverrideMetadataKeys = MaterialOverrides.GetMetadataKeys();

	for (const FString& MaterialOverrideKey : MaterialOverrideMetadataKeys)
	{
		HashKeys.AddUnique(MaterialOverrideKey);
	}

	return GetView()->GetValuesAndTransformsHash(HashKeys);
}

bool FOneActorRuleInstance::Execute(FSliceAndDiceExecutionContextPtr Context)
{
	check(PointCloud);

	int32 ResultCount = GetView()->GetCount();

	// save the stats if we're in the right reporting mode
	if (GenerateReporting())
	{
		// record the statistics for the given view		
		ReportFrame->AddParameter(TEXT("Module Count"), FString::FromInt(ResultCount));
	}

	const FString Name = UOneActorBuildRule::MakeName(PointCloud, Data.NamePattern, Data.NameValue);

	ReportFrame->AddParameter(TEXT("Name"), Name);

	if (!GenerateAssets() || ResultCount==0)
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
	}

	// if the per module attribute key exists on the PC, add it
	if (GetView()->GetPointCloud()->HasMetaDataAttribute(Data.PerModuleAttributeKey))
	{
		Params.PerModuleAttributeKey = Data.PerModuleAttributeKey;
	}
	Params.bManualGroupId = Data.bManualGroupId;
	Params.GroupId = Data.GroupId;

	AActor* NewActor = UPointCloudAssetsHelpers::CreateActorFromView(GetView(), Name, Params);

	if (NewActor)
	{
		NewActorAdded(NewActor, GetView());
		return true;
	}
	else
	{
		return false;
	}
}

FString FOneActorBuildFactory::Name() const
{
	return OneActorBuildRuleConstants::Name;
}

FString FOneActorBuildFactory::Description() const
{
	return OneActorBuildRuleConstants::Description;
}

UPointCloudRule* FOneActorBuildFactory::Create(UObject *parent)
{
	UPointCloudRule* Result = NewObject<UOneActorBuildRule>(parent);	
	return Result;
}

FOneActorBuildFactory::FOneActorBuildFactory(TSharedPtr<ISlateStyle> Style)
{
	FSlateStyleSet* AsStyleSet = static_cast<FSlateStyleSet*>(Style.Get());
	if (AsStyleSet)
	{
		Icon = new FSlateImageBrush(AsStyleSet->RootToContentDir(TEXT("Resources/SingleObjectRule"), TEXT(".png")), FVector2D(128.f, 128.f));
		AsStyleSet->Set(TEXT("RuleThumbnail.SingleObjectRule"), Icon);
	}
	else
	{
		Icon = nullptr;
	}
}

FOneActorBuildFactory::~FOneActorBuildFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FOneActorBuildFactory::GetIcon() const
{ 
	return Icon; 
}