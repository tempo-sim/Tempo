// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

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
class TEMPOVEHICLES_API UTempoDrivingServiceSubsystem : public UWorldSubsystem, public ITempoWorldScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;

private:
	void GetCommandableVehicles(const TempoScripting::Empty& Request, const TResponseDelegate<TempoVehicles::CommandableVehiclesResponse>& ResponseContinuation) const;
	
	void HandleDrivingCommand(const TempoVehicles::DrivingCommandRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
};
