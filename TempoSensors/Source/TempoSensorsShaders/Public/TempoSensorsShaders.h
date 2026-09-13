// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTempoSensorsShaders, Log, All);

class FTempoSensorsShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
