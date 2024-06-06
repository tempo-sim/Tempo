// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoTime.h"

#define LOCTEXT_NAMESPACE "FTempoTimeModule"

DEFINE_LOG_CATEGORY(LogTempoTime);

void FTempoTimeModule::StartupModule()
{
}

void FTempoTimeModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoTimeModule, TempoTime)
