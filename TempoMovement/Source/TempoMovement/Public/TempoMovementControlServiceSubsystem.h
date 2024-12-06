// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoMovementControlServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoMovement
{
	class VehicleCommandRequest;
	class CommandableVehiclesResponse;
}

UCLASS()
class TEMPOMOVEMENT_API UTempoMovementControlServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void GetCommandableVehicles(const TempoScripting::Empty& Request, const TResponseDelegate<TempoMovement::CommandableVehiclesResponse>& ResponseContinuation) const;
	
	void CommandVehicle(const TempoMovement::VehicleCommandRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
};
