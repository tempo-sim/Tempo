#include "TempoMovementShared.h"

#define LOCTEXT_NAMESPACE "FTempoMovementSharedModule"

DEFINE_LOG_CATEGORY(LogTempoMovementShared);

void FTempoMovementSharedModule::StartupModule()
{
    
}

void FTempoMovementSharedModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FTempoMovementSharedModule, TempoMovementShared)
