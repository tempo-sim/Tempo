// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Loads before TempoCore, to let the loader find gRPC's shared library where TempoThirdParty puts
// it. Only Windows needs the help: on Mac and Linux, TempoCore's own rpath points to it.
class FTempoCoreBootstrapModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
