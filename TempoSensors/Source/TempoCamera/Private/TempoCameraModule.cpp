// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoCameraModule.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FTempoCameraModule"

DEFINE_LOG_CATEGORY(LogTempoCamera);

void FTempoCameraModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TempoSensors"));
	if (Plugin.IsValid())
	{
		FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/TempoSensors"), PluginShaderDir);
		UE_LOG(LogTempoCamera, Log, TEXT("Mapped shader directory: /TempoSensors -> %s"), *PluginShaderDir);
	}
	else
	{
		UE_LOG(LogTempoCamera, Error, TEXT("Failed to find TempoSensors plugin. Shader directory mapping not added."));
	}
}

void FTempoCameraModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTempoCameraModule, TempoCamera)
