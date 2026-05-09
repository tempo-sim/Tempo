// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "TempoServer.h"
#include "TempoServiceProvider.h"

#include "CoreMinimal.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "TempoAgentsEditorServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
}

namespace TempoAgentsEditor
{
	class PipelineResult;
}

UCLASS()
class TEMPOAGENTSEDITOR_API UTempoAgentsEditorServiceSubsystem : public UUnrealEditorSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;
	
	void RunTempoZoneGraphBuilderPipeline(const TempoCore::Empty& Request, const TResponseDelegate<TempoAgentsEditor::PipelineResult>& ResponseContinuation) const;
};
