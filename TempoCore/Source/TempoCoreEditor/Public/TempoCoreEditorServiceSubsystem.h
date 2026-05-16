// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"

#include "CoreMinimal.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "TempoCoreEditorServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
}

namespace TempoCoreEditor
{
	class SaveLevelRequest;
	class OpenLevelRequest;
}

UCLASS()
class TEMPOCOREEDITOR_API UTempoCoreEditorServiceSubsystem : public UUnrealEditorSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	void RegisterServices(FTempoServer& Server) override;

	void Initialize(FSubsystemCollectionBase& Collection) override;

	void Deinitialize() override;

	void PlayInEditor(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void Simulate(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void Stop(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void SaveLevel(const TempoCoreEditor::SaveLevelRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void OpenLevel(const TempoCoreEditor::OpenLevelRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void NewLevel(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);
};
