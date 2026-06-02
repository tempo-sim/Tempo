// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoTimeServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
	class TimeModeRequest;
	class SetSimStepsPerSecondRequest;
	class AdvanceStepsRequest;
	class GetSimTimeResponse;
}

UCLASS()
class TEMPOCORE_API UTempoTimeServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void SetTimeMode(const TempoCore::TimeModeRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void SetSimStepsPerSecond(const TempoCore::SetSimStepsPerSecondRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void AdvanceSteps(const TempoCore::AdvanceStepsRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void Play(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void Pause(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void Step(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void GetSimTime(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::GetSimTimeResponse>& ResponseContinuation) const;
};
