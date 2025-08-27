#include "TempoLidarModule.h"

#define LOCTEXT_NAMESPACE "FTempoLidarModule"

DEFINE_LOG_CATEGORY(LogTempoLidar);

void FTempoLidarModule::StartupModule()
{
    
}

void FTempoLidarModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FTempoLidarModule, TempoLidar)
