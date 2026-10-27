// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"

#include "TempoConsoleServiceSubsystem.generated.h"

namespace TempoCore
{
	class SetConsoleVariableRequest;
	class GetConsoleVariableRequest;
	class ConsoleVariableResponse;
}

// An engine subsystem, rather than a game instance one, because console variables outlive any world:
// this way the service is active in the editor outside of a play session as well as in a packaged build.
UCLASS()
class TEMPOCORE_API UTempoConsoleServiceSubsystem : public UEngineSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void SetConsoleVariable(const TempoCore::SetConsoleVariableRequest& Request, const TResponseDelegate<TempoCore::ConsoleVariableResponse>& ResponseContinuation) const;

	void GetConsoleVariable(const TempoCore::GetConsoleVariableRequest& Request, const TResponseDelegate<TempoCore::ConsoleVariableResponse>& ResponseContinuation) const;
};
