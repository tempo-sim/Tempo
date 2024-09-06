// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditorServiceSubsystem.h"

#include "TempoAgentsEditorUtils.h"
#include "TempoAgentsEditor/TempoAgentsEditor.grpc.pb.h"

using TempoAgentsEditorService = TempoAgentsEditor::TempoAgentsEditorService::AsyncService;

void UTempoAgentsEditorServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer* ScriptingServer)
{
	ScriptingServer->RegisterService<TempoAgentsEditorService>(
		TSimpleRequestHandler<TempoAgentsEditorService, TempoScripting::Empty, TempoScripting::Empty>(&TempoAgentsEditorService::RequestRunTempoZoneGraphBuilderPipeline).BindUObject(this, &UTempoAgentsEditorServiceSubsystem::RunTempoZoneGraphBuilderPipeline)
	);
}

void UTempoAgentsEditorServiceSubsystem::RunTempoZoneGraphBuilderPipeline(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	UTempoAgentsEditorUtils::RunTempoZoneGraphBuilderPipeline();

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}
