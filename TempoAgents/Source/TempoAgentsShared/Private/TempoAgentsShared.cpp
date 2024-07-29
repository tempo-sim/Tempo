#include "TempoAgentsShared.h"

#define LOCTEXT_NAMESPACE "FTempoAgentsSharedModule"

DEFINE_LOG_CATEGORY(LogTempoAgentsShared);

void FTempoAgentsSharedModule::StartupModule()
{
    
}

void FTempoAgentsSharedModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FTempoAgentsSharedModule, TempoAgentsShared)
