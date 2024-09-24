// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoWorldSubsystem.h"

#include "CoreMinimal.h"

#include "TempoVehicleControlServiceSubsystem.generated.h"

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
class TEMPOMOVEMENT_API UTempoVehicleControlServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterScriptingServices(FTempoScriptingServer* ScriptingServer) override;

protected:
	void GetCommandableVehicles(const TempoScripting::Empty& Request, const TResponseDelegate<TempoMovement::CommandableVehiclesResponse>& ResponseContinuation) const;
	
	void HandleVehicleCommand(const TempoMovement::VehicleCommandRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
};
