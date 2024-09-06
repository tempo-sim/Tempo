// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditorStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FTempoAgentsEditorStyle::StyleInstance = nullptr;

void FTempoAgentsEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FTempoAgentsEditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FTempoAgentsEditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("TempoAgentsStyle"));
	return StyleSetName;
}

TSharedRef< FSlateStyleSet > FTempoAgentsEditorStyle::Create()
{
	const FVector2D Icon20x20(20.0f, 20.0f);

	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("TempoAgentsStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("TempoAgents"))->GetBaseDir() / TEXT("Resources"));

	Style->Set("TempoAgents.PluginAction", new IMAGE_BRUSH_SVG(TEXT("Merge"), Icon20x20));
	return Style;
}

void FTempoAgentsEditorStyle::ReloadTextures() 
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FTempoAgentsEditorStyle::Get()
{
	return *StyleInstance;
}
