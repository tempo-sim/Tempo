// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreBootstrap.h"

#include "Interfaces/IPluginManager.h"

void FTempoCoreBootstrapModule::StartupModule()
{
#if PLATFORM_WINDOWS && !IS_MONOLITHIC
	// TempoCore.dll imports tempogrpc.dll, which Windows looks for beside the executable, not beside
	// the importing DLL. Unreal resolves a module's imports from its registered DLL directories
	// first, so register the one the TempoThirdParty release puts tempogrpc.dll in.
	const TSharedPtr<IPlugin> TempoCorePlugin = IPluginManager::Get().FindPlugin(TEXT("TempoCore"));
	if (TempoCorePlugin.IsValid())
	{
		FPlatformProcess::AddDllDirectory(*FPaths::Combine(TempoCorePlugin->GetBaseDir(), TEXT("Source"), TEXT("ThirdParty"), TEXT("gRPC"), TEXT("Binaries"), TEXT("Windows")));
	}
#endif
}

IMPLEMENT_MODULE(FTempoCoreBootstrapModule, TempoCoreBootstrap)
