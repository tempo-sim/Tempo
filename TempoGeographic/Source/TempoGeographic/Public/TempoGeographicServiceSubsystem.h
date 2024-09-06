// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoWorldSubsystem.h"

#include "CoreMinimal.h"

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
class TEMPOGEOGRAPHIC_API UTempoGeographicServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer* ScriptingServer) override;

protected:
	void SetDate(const TempoGeographic::Date& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void SetTimeOfDay(const TempoGeographic::TimeOfDay& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void SetDayCycleRelativeRate(const TempoGeographic::DayCycleRateRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void GetDateTime(const TempoScripting::Empty& Request, const TResponseDelegate<TempoGeographic::DateTime>& ResponseContinuation);

	void SetGeographicReference(const TempoGeographic::GeographicCoordinate& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);
};
