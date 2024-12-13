// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "TempoCoreEditorServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoCoreEditor
{
	class SaveLevelRequest;
	class OpenLevelRequest;
}

UCLASS()
class TEMPOCOREEDITOR_API UTempoCoreEditorServiceSubsystem : public UUnrealEditorSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;
	
	void Initialize(FSubsystemCollectionBase& Collection) override;

	void Deinitialize() override;
	
	void PlayInEditor(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void Simulate(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void Stop(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void SaveLevel(const TempoCoreEditor::SaveLevelRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void OpenLevel(const TempoCoreEditor::OpenLevelRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void NewLevel(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);
};
