// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExportFBXRule.h"
#include "PointCloudAssetHelpers.h"

#include "AssetExportTask.h"
#include "Brushes/SlateImageBrush.h"
#include "Engine/World.h"
#include "Exporters/Exporter.h"
#include "Exporters/FbxExportOption.h"
#include "Misc/MessageDialog.h"
#include "Styling/SlateStyle.h"
#include "UObject/GCObjectScopeGuard.h"

#define LOCTEXT_NAMESPACE "RuleProcessorExportFBXRule"

namespace ExportFBXRule
{
	static const FString Description = TEXT("Export an FBX file with instances from the given point cloud");
	static const FString Name = TEXT("Export FBX");
}

FExportFBXRuleData::FExportFBXRuleData()
{
	NamePattern = TEXT("$IN_VALUE_$RULEPROCESSOR_ASSET");

	RegisterOverrideableProperty(TEXT("NamePattern"));
}

void FExportFBXRuleData::OverrideNameValue()
{
	FString Name = NamePattern;
	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	NameValue = Name;
}

UExportFBXRule::UExportFBXRule()
	: UPointCloudRule(&Data)
{
	InitSlots(1);
}

FString UExportFBXRule::Description() const
{
	return ExportFBXRule::Description;
}

FString UExportFBXRule::RuleName() const
{
	return ExportFBXRule::Name;
}

FString UExportFBXRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	return FString(TEXT("Export"));
}

FString UExportFBXRule::MakeName(UPointCloud* Pc, const FString& InNamePattern)
{
	if (Pc == nullptr)
	{
		return FString();
	}

	FString Result = InNamePattern;

	Result.ReplaceInline(TEXT("$RULEPROCESSOR_ASSET"), *Pc->GetName());
	Result.ReplaceInline(TEXT("$MANTLE_ASSET"), *Pc->GetName());

	return Result;
}

void UExportFBXRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
}

bool UExportFBXRule::Compile(FSliceAndDiceContext& Context) const
{
	OverwriteAllFiles.Reset();

	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}	

	if (Data.ExportDirectory.Path.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty directory path in Export FBX rule"));
		return false;
	}

	bool bResult = false;

	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FExportFBXRuleInstance(this));

			Instance.EmitInstance(RuleInstance, GetSlotName(0));
			bResult |= Slot->Compile(Context);
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return bResult;
}

bool FExportFBXRuleInstance::Execute()
{
	// Override world for downstream rule instances
	Data.World = UWorld::CreateWorld(EWorldType::None, false);
	Data.AddOverridenProperty("World");

	Data.OverrideNameValue();

	return true;
}

bool FExportFBXRuleInstance::PostExecute()
{

	if (!GenerateAssets())
	{
		return true;
	}

	const FString Name = UExportFBXRule::MakeName(GetPointCloud(), Data.NameValue);
	const FString Filename = Data.ExportDirectory.Path / Name + TEXT(".fbx");

	const UExportFBXRule* ExportFBXRule = Cast<UExportFBXRule>(GetRule());
	check(ExportFBXRule);

	bool bDoExport = ExportFBXRule->OverwriteAllFiles.Get(true);
	if (!ExportFBXRule->OverwriteAllFiles.IsSet() && !Data.bOverwriteExistingFile && FPaths::FileExists(Filename))
	{
		const FText DialogTitle = FText::FromString(Filename);
		const FText DialogMessage = LOCTEXT("ExportFBXRule_DialogMessage", "The FBX file already exists would you like to overwrite it?");
		switch (FMessageDialog::Open(EAppMsgType::YesNoYesAllNoAll, DialogMessage, DialogTitle))
		{
		case EAppReturnType::Yes:
			bDoExport = true;
			break;
		case EAppReturnType::No:
			bDoExport = false;
			break;
		case EAppReturnType::YesAll:
			ExportFBXRule->OverwriteAllFiles = true;
			bDoExport = true;
			break;
		case EAppReturnType::NoAll:
			ExportFBXRule->OverwriteAllFiles = false;
			bDoExport = false;
			break;
		default:
			return true;
			break;
		}
	}

	if (bDoExport)
	{
		UFbxExportOption* ExportOptions = NewObject<UFbxExportOption>();
		UAssetExportTask* ExportTask = NewObject<UAssetExportTask>();
		FGCObjectScopeGuard ExportTaskGuard(ExportTask);
		ExportTask->Object = Data.World;
		ExportTask->Exporter = nullptr;
		ExportTask->Filename = Filename;
		ExportTask->bSelected = false;
		ExportTask->bReplaceIdentical = false;
		ExportTask->bPrompt = false;
		ExportTask->bUseFileArchive = true;
		ExportTask->bWriteEmptyFiles = false;
		ExportTask->bAutomated = Data.bAutomated;
		ExportTask->Options = ExportOptions;
		UExporter::RunAssetExportTask(ExportTask);
	}

	Data.World->DestroyWorld(false);

	return true;
}

FString FExportFBXFactory::Name() const
{
	return ExportFBXRule::Name;
}

FString FExportFBXFactory::Description() const
{
	return ExportFBXRule::Description;
}

UPointCloudRule* FExportFBXFactory::Create(UObject* parent)
{
	UPointCloudRule* Result = NewObject<UExportFBXRule>(parent);
	return Result;
}

FExportFBXFactory::FExportFBXFactory(TSharedPtr<ISlateStyle> Style)
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

FExportFBXFactory::~FExportFBXFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FExportFBXFactory::GetIcon() const
{
	return Icon;
}

#undef LOCTEXT_NAMESPACE
