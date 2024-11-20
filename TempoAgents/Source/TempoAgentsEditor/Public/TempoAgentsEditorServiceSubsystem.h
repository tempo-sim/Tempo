// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "TempoScriptingServer.h"
#include "TempoScriptable.h"

#include "CoreMinimal.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "TempoAgentsEditorServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

UCLASS()
class TEMPOAGENTSEDITOR_API UTempoAgentsEditorServiceSubsystem : public UUnrealEditorSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;
	
	void RunTempoZoneGraphBuilderPipeline(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
};
