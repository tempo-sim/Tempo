// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"

#include "TempoEngineServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
	class SetConsoleVariableRequest;
	class GetConsoleVariableRequest;
	class ConsoleVariableResponse;
	class ExecConsoleCommandRequest;
	class ExecConsoleCommandResponse;
	class EngineInfoResponse;
}

// An engine subsystem, rather than a game instance one, so that its service outlives any world: it is
// active in the editor outside of a play session as well as in a packaged build.
UCLASS()
class TEMPOCORE_API UTempoEngineServiceSubsystem : public UEngineSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void SetConsoleVariable(const TempoCore::SetConsoleVariableRequest& Request, const TResponseDelegate<TempoCore::ConsoleVariableResponse>& ResponseContinuation) const;

	void GetConsoleVariable(const TempoCore::GetConsoleVariableRequest& Request, const TResponseDelegate<TempoCore::ConsoleVariableResponse>& ResponseContinuation) const;

	void ExecConsoleCommand(const TempoCore::ExecConsoleCommandRequest& Request, const TResponseDelegate<TempoCore::ExecConsoleCommandResponse>& ResponseContinuation) const;

	void GetEngineInfo(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::EngineInfoResponse>& ResponseContinuation) const;
};
