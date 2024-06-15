// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudEditorModule.h"
#include "Containers/Array.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "PointCloudSliceAndDiceRuleSet.h"

#include "Rules/ConsolidateBlueprintISMsToActorBuildRule.h"
#include "Rules/DebugBuildRule.h"
#include "Rules/ExportFBXRule.h"
#include "Rules/FilterOnBoundingBox.h"
#include "Rules/FilterOnOrientedBoundingBoxIterator.h"
#include "Rules/FilterOnTile.h"
#include "Rules/FilterOnTileIterator.h"
#include "Rules/MetadataFilterRule.h"
#include "Rules/MetadataIteratorRule.h"
#include "Rules/MultiActorBuildRule.h"
#include "Rules/OneActorBuildRule.h"
#include "Rules/SpawnBlueprintsBuildRule.h"
#include "Rules/SpawnPackedBlueprintsBuildRule.h"
#include "Rules/VertexExpressionRule.h"
#include "Rules/ExternalRule.h"
#include "Rules/PerPointIterator.h"
#include "Rules/SequenceRule.h"
#include "Rules/SpawnNiagaraRule.h"
#include "Rules/ExecuteBlueprintRule.h"

#include "AssetTools/PointCloudActions.h"
#include "AssetTools/PointCloudBlueprintActions.h"
#include "AssetTools/PointCloudSliceAndDiceRulesActions.h"
#include "Styles/PointCloudEditorStyle.h"
#include "PointCloudEditorSettings.h"

#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SSliceAndDiceManagerWidget.h"
#include "PropertyEditorModule.h"
 
#define LOCTEXT_NAMESPACE "FPointCloudEditorModule"
 
/**
 * Implements the PointCloudEditor module.
 */
class FPointCloudEditorModule
	: public IHasMenuExtensibility
	, public IHasToolBarExtensibility
	, public IPointCloudEditorModule
{
public:

	//~ IHasMenuExtensibility interface

	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override
	{
		return MenuExtensibilityManager;
	}

public:

	//~ IHasToolBarExtensibility interface

	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override
	{
		return ToolBarExtensibilityManager;
	}

public:

	//~ IPointCloudEditorModule interface

	virtual EAssetTypeCategories::Type GetAssetCategory() const override
	{
		return PointCloudAssetCategory;
	}

public:

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
		Style = MakeShareable(new FPointCloudEditorStyle());

//		FPointCloudEditorCommands::Register();

		RegisterAssetTools();
		RegisterMenuExtensions();
		RegisterSettings();
		RegisterDetailsCustomizations();
		RegisterRules();

		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
		{
			LevelEditorModule->OnMapChanged().AddRaw(this, &FPointCloudEditorModule::OnMapChanged);
		}
	}

	virtual void ShutdownModule() override
	{
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
		{
			LevelEditorModule->OnMapChanged().RemoveAll(this);
		}

		UnregisterAssetTools();
		UnregisterMenuExtensions();
		UnregisterSettings();
		UnRegisterDetailsCustomizations();
		UnRegisterRules();
	}

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

	virtual bool RegisterRules()
	{
		// Generator Rules
		RuleFactories.Add(new FOneActorBuildFactory(Style));
		RuleFactories.Add(new FSpawnBlueprintsBuildFactory(Style));
		RuleFactories.Add(new FSpawnPackedBlueprintsBuildFactory(Style));
		RuleFactories.Add(new FMultiActorBuildFactory(Style));
		RuleFactories.Add(new FExportFBXFactory(Style));
		RuleFactories.Add(new FDebugBuildFactory(Style));
		RuleFactories.Add(new FExternalRuleFactory(Style));
		RuleFactories.Add(new FSpawnNiagaraFactory(Style));
		RuleFactories.Add(new FConsolidateBlueprintISMsToActorBuildFactory(Style));
		RuleFactories.Add(new FExecuteBlueprintFactory());

		// Filter Rules
		RuleFactories.Add(new FBoundingBoxFilterFactory);
		RuleFactories.Add(new FTileFilterFactory);
		RuleFactories.Add(new FVertexExpressionRuleFactory);
		RuleFactories.Add(new FMetadataFilterRuleFactory);
		RuleFactories.Add(new FMetadataIteratorRuleFactory);
		RuleFactories.Add(new FTileIteratorFilterFactory);
		RuleFactories.Add(new FOrientedBoundingBoxIteratorFilterFactory);
		RuleFactories.Add(new FPerPointIteratorFilterFactory);
		RuleFactories.Add(new FSequenceRuleFactory);

		
		// Register all of the factories with the slice and dice system
		for (FSliceAndDiceRuleFactory* RuleFactory : RuleFactories)
		{
			UPointCloudSliceAndDiceRuleSet::RegisterRuleFactory(RuleFactory);
		}

		return true;
	}

	virtual bool UnRegisterRules()
	{
		for (FSliceAndDiceRuleFactory* RuleFactory : RuleFactories)
		{
			UPointCloudSliceAndDiceRuleSet::DeleteFactory(RuleFactory);
		}

		RuleFactories.Empty();

		return true;
	}

protected:

	/** Registers asset tool actions. */
	void RegisterAssetTools()
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		PointCloudAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("PointCloud")), LOCTEXT("PointCloudAssetCategory", "Rule Processor"));

		RegisterAssetTypeAction(AssetTools, MakeShareable(new FPointCloudActions(Style.ToSharedRef())));
		RegisterAssetTypeAction(AssetTools, MakeShareable(new FPointCloudSliceAndDiceRulesActions(Style.ToSharedRef())));
		RegisterAssetTypeAction(AssetTools, MakeShareable(new FPointCloudBlueprintActions()));
	}

	void RegisterDetailsCustomizations()
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		//Custom properties 		

		PropertyModule.NotifyCustomizationModuleChanged();
	}

	void UnRegisterDetailsCustomizations()
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			auto& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");						
		}
	}

	/**
	 * Registers a single asset type action.
	 *
	 * @param AssetTools The asset tools object to register with.
	 * @param Action The asset type action to register.
	 */
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		RegisteredAssetTypeActions.Add(Action);
	}

	/** Register the point cloud editor settings. */
	void RegisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		if (SettingsModule != nullptr)
		{
			ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "PointCloud",
				LOCTEXT("PointCloudSettingsName", "Point Cloud"),
				LOCTEXT("PointCloudSettingsDescription", "Configure the Rule Processor plug-in."),
				GetMutableDefault<UPointCloudEditorSettings>()
			);
		}
	}

	/** Unregisters asset tool actions. */
	void UnregisterAssetTools()
	{
		FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

		if (AssetToolsModule != nullptr)
		{
			IAssetTools& AssetTools = AssetToolsModule->Get();

			for (auto Action : RegisteredAssetTypeActions)
			{
				AssetTools.UnregisterAssetTypeActions(Action);
			}
		}
	}

	/** Unregister the point cloud editor settings. */
	void UnregisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "PointCloud");
		}
	}

protected:

	/** Registers main menu and tool bar menu extensions. */
	void RegisterMenuExtensions()
	{
		MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
		ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

		{
			TSharedPtr<FExtender> NewMenuExtender = MakeShareable(new FExtender);
			NewMenuExtender->AddMenuExtension("LevelEditor",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateRaw(this, &FPointCloudEditorModule::AddMenuEntry));

			LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(NewMenuExtender);
		}
	}

	/** Unregisters main menu and tool bar menu extensions. */
	void UnregisterMenuExtensions()
	{
		MenuExtensibilityManager.Reset();
		ToolBarExtensibilityManager.Reset();
	}

	virtual void OnMapChanged(UWorld* World, EMapChangeType ChangeType)
	{
		SliceAndDiceTabManager::OnMapChanged(World, ChangeType);
	}

private:

	static void OpenSliceAndDiceManager(ASliceAndDiceManager* InManager)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		SliceAndDiceTabManager::OpenTab(LevelEditorModule.GetLevelEditorTabManager().ToSharedRef(), InManager);
	}

	void AddMenuEntry(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection("RuleProcessorMenu", TAttribute<FText>(FText::FromString("Rule Processor Tools")));

		MenuBuilder.AddSubMenu(
			LOCTEXT("OpenManager", "Rule Processor"),
			LOCTEXT("OpenManager_Tooltip", "Open Rule Processor"),
			FNewMenuDelegate::CreateRaw(this, &FPointCloudEditorModule::PopulateManagerActions));

		MenuBuilder.EndSection();
	}

	void PopulateManagerActions(FMenuBuilder& MenuBuilder)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		TArray<ASliceAndDiceManager*> ExistingManagers = ASliceAndDiceManager::GetSliceAndDiceManagers(World);

		for (ASliceAndDiceManager* Manager : ExistingManagers)
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(Manager->GetActorLabel()),
				FText::FromString(FString::Printf(TEXT("Open %s Rule Processor"), *(Manager->GetActorLabel()))),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([Manager]() {
						OpenSliceAndDiceManager(Manager);
						})),
				NAME_None);
		}

		if (ExistingManagers.Num() > 0)
		{
			MenuBuilder.AddSeparator();
		}
		
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateNewManager", "Create New"),
			LOCTEXT("CreateNewManager_Tooltip", "Creates a new Rule Processor"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([]() {
					UWorld* World = GEditor->GetEditorWorldContext().World();
					if (World)
					{
						OpenSliceAndDiceManager(ASliceAndDiceManager::CreateSliceAndDiceManager(World));
					}
				})),
			NAME_None);
	}

	/** Holds the menu extensibility manager. */
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;

	/** The collection of registered asset type actions. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;

	/** Holds the plug-ins style set. */
	TSharedPtr<ISlateStyle> Style;

	/** Holds the tool bar extensibility manager. */
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	TArray<FSliceAndDiceRuleFactory*> RuleFactories;

	TSharedPtr<class FUICommandList> PluginCommands;

protected:
	EAssetTypeCategories::Type PointCloudAssetCategory;
};


IMPLEMENT_MODULE(FPointCloudEditorModule, PointCloudEditor);


#undef LOCTEXT_NAMESPACE
