// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Loads before TempoCore, to let the loader find gRPC's shared library where TempoThirdParty puts
// it. Only Windows needs the help: on Mac and Linux, TempoCore's own rpath points to it.
// Also, on Windows, installs the Ctrl-C handler Unreal skips when the process has no console
// window, as under a CI runner service.
class FTempoCoreBootstrapModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
