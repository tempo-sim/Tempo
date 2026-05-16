// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

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

namespace TempoCore
{
	class Empty;
}

UCLASS()
class TEMPOGEOGRAPHIC_API UTempoGeographicServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void SetDate(const TempoGeographic::Date& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void SetTimeOfDay(const TempoGeographic::TimeOfDay& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void SetDayCycleRelativeRate(const TempoGeographic::DayCycleRateRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void GetDateTime(const TempoCore::Empty& Request, const TResponseDelegate<TempoGeographic::DateTime>& ResponseContinuation);

	void SetGeographicReference(const TempoGeographic::GeographicCoordinate& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);
};
