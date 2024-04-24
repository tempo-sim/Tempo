// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoCoreShared.h"

#define LOCTEXT_NAMESPACE "FTempoCoreSharedModule"

DEFINE_LOG_CATEGORY(LogTempoCoreShared);

void FTempoCoreSharedModule::StartupModule()
{
}

void FTempoCoreSharedModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoCoreSharedModule, TempoCoreShared)
