// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoMovement.h"

#define LOCTEXT_NAMESPACE "FTempoMovementModule"

DEFINE_LOG_CATEGORY(LogTempoMovement);

void FTempoMovementModule::StartupModule()
{
}

void FTempoMovementModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoMovementModule, TempoMovement)
