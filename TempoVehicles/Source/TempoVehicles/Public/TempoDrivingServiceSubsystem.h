// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoWorldSubsystem.h"

#include "CoreMinimal.h"

#include "TempoDrivingServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoVehicles
{
	class DrivingCommandRequest;
	class CommandableVehiclesResponse;
}

UCLASS()
class TEMPOVEHICLES_API UTempoDrivingServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterScriptingServices(FTempoScriptingServer* ScriptingServer) override;

protected:
	void GetCommandableVehicles(const TempoScripting::Empty& Request, const TResponseDelegate<TempoVehicles::CommandableVehiclesResponse>& ResponseContinuation) const;
	
	void HandleDrivingCommand(const TempoVehicles::DrivingCommandRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
};
