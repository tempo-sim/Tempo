// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoVehicles.h"

#define LOCTEXT_NAMESPACE "FTempoVehiclesModule"

DEFINE_LOG_CATEGORY(LogTempoVehicles);

void FTempoVehiclesModule::StartupModule()
{
}

void FTempoVehiclesModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoVehiclesModule, TempoVehicles)
