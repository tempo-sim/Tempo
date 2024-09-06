// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditor.h"

#include "TempoAgentsEditorCommands.h"
#include "TempoAgentsEditorStyle.h"
#include "TempoAgentsEditorUtils.h"

#include "ToolMenus.h"

DEFINE_LOG_CATEGORY(LogTempoAgentsEditor);

#define LOCTEXT_NAMESPACE "FTempoAgentsEditorModule"

void FTempoAgentsEditorModule::StartupModule()
{
	FTempoAgentsEditorStyle::Initialize();
	FTempoAgentsEditorStyle::ReloadTextures();

	FTempoAgentsEditorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FTempoAgentsEditorCommands::Get().PluginAction,
		FExecuteAction::CreateStatic(&UTempoAgentsEditorUtils::RunTempoZoneGraphBuilderPipeline),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FTempoAgentsEditorModule::RegisterMenus));
}

void FTempoAgentsEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FTempoAgentsEditorStyle::Shutdown();

	FTempoAgentsEditorCommands::Unregister();
}

void FTempoAgentsEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FTempoAgentsEditorCommands::Get().PluginAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolbar.PlayToolbar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FTempoAgentsEditorCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoAgentsEditorModule, TempoAgentsEditor)
