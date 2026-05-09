// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTempoCore, Log, All);

class FTempoServer;

class FTempoCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// Tempo gRPC server. TempoCore is the unique owner of the gRPC/protobuf
	// dllexport boundary in v2, so it owns the server lifetime. See
	// TempoCore.Build.cs for the matching PublicDefinitions and
	// bCanHotReload=false explanation.
	TUniquePtr<FTempoServer> Server;

	friend FTempoServer;
};
