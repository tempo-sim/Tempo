// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsShaders.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogTempoSensorsShaders);

void FTempoSensorsShadersModule::StartupModule()
{
	// Maps /Plugin/TempoSensors to the plugin's Shaders directory. This module loads at
	// PostConfigInit, so the mapping exists before the global shader map is compiled.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TempoSensors"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogTempoSensorsShaders, Error, TEXT("TempoSensors plugin not found; its shaders will not be available."));
		return;
	}

	const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/TempoSensors"), ShaderDirectory);
}

IMPLEMENT_MODULE(FTempoSensorsShadersModule, TempoSensorsShaders)
