// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditorServiceSubsystem.h"

#include "TempoAgentsEditorUtils.h"
#include "TempoAgentsEditor/TempoAgentsEditor.grpc.pb.h"

using TempoAgentsEditorService = TempoAgentsEditor::TempoAgentsEditorService::AsyncService;

DEFINE_TEMPO_SERVICE_TYPE_TRAITS(TempoAgentsEditorService);

void UTempoAgentsEditorServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<TempoAgentsEditorService>(
		SimpleRequestHandler(&TempoAgentsEditorService::RequestRunTempoZoneGraphBuilderPipeline, &UTempoAgentsEditorServiceSubsystem::RunTempoZoneGraphBuilderPipeline)
	);
}

void UTempoAgentsEditorServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().BindObjectToService<TempoAgentsEditorService>(this);
}

void UTempoAgentsEditorServiceSubsystem::RunTempoZoneGraphBuilderPipeline(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	UTempoAgentsEditorUtils::RunTempoZoneGraphBuilderPipeline();

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}
