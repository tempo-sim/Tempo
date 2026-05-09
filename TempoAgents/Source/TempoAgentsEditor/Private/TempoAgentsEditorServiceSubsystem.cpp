// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditorServiceSubsystem.h"

#include "TempoAgentsEditorUtils.h"
#include "TempoAgentsEditor/TempoAgentsEditor.grpc.pb.h"

using TempoAgentsEditorService = TempoAgentsEditor::TempoAgentsEditorService;
using TempoAgentsEditorAsyncService = TempoAgentsEditor::TempoAgentsEditorService::AsyncService;

void UTempoAgentsEditorServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<TempoAgentsEditorService>(
		SimpleRequestHandler(&TempoAgentsEditorAsyncService::RequestRunTempoZoneGraphBuilderPipeline, &UTempoAgentsEditorServiceSubsystem::RunTempoZoneGraphBuilderPipeline)
	);
}

void UTempoAgentsEditorServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<TempoAgentsEditorService>(this);
}

void UTempoAgentsEditorServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<TempoAgentsEditorService>();
}

void UTempoAgentsEditorServiceSubsystem::RunTempoZoneGraphBuilderPipeline(const TempoCore::Empty& Request, const TResponseDelegate<TempoAgentsEditor::PipelineResult>& ResponseContinuation) const
{
	const bool bSuccess = UTempoAgentsEditorUtils::RunTempoZoneGraphBuilderPipeline();

	TempoAgentsEditor::PipelineResult Result;
	Result.set_success(bSuccess);

	ResponseContinuation.ExecuteIfBound(Result, grpc::Status_OK);
}
