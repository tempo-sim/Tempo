// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoCore.h"

#include "TempoServer.h"

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

#if WITH_EDITOR
#include "IHotReload.h"
#endif

#define LOCTEXT_NAMESPACE "FTempoCoreModule"

DEFINE_LOG_CATEGORY(LogTempoCore);

void FTempoCoreModule::StartupModule()
{
	Server = MakeUnique<FTempoServer>();

#if WITH_EDITOR
	// We disallow hot reload for TempoCore (see bCanHotReload=false in
	// TempoCore.Build.cs) for the same reason we force TempoCore to re-export
	// gRPC/protobuf symbols: those libraries hold static global state that must
	// live in exactly one place in the executable. Hot-reloading TempoCore would
	// duplicate the dll while leaving callers pointing at the old one — yuck.
	//
	// Other modules that define protos can still hot-reload, but Protobuf
	// crashes when freshly-regenerated message classes try to register
	// themselves with its global descriptor pool. Reset that memory at the
	// start of each hot reload so the new classes can register cleanly.
	IHotReloadModule::Get().OnModuleCompilerStarted().AddLambda([](bool)
	{
		// Note: these reset methods were added in TempoThirdParty-v0.4.
		google::protobuf_tempo::DescriptorPool::ResetGeneratedDatabase();
		google::protobuf_tempo::MessageFactory::ResetGeneratedFactory();
	});
#endif
}

void FTempoCoreModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTempoCoreModule, TempoCore)
