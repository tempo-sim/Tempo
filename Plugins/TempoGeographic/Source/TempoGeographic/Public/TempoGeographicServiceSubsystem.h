// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TempoGeographicServiceSubsystem.generated.h"

namespace TempoGeographic
{
	class Date;
	class TimeOfDay;
	class DateTime;
	class GeographicCoordinate;
	class DayCycleRateRequest;
}

namespace TempoScripting
{
	class Empty;
}

UCLASS()
class TEMPOGEOGRAPHIC_API UTempoGeographicServiceSubsystem : public UWorldSubsystem, public ITempoWorldScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;

private:
	void SetDate(const TempoGeographic::Date& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void SetTimeOfDay(const TempoGeographic::TimeOfDay& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void SetDayCycleRelativeRate(const TempoGeographic::DayCycleRateRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void GetDateTime(const TempoScripting::Empty& Request, const TResponseDelegate<TempoGeographic::DateTime>& ResponseContinuation);

	void SetOriginGeographicCoord(const TempoGeographic::GeographicCoordinate& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);
};
