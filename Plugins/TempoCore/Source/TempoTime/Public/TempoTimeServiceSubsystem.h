// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoTimeServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoTime
{
	class TimeModeRequest;
	class SetSimStepsPerSecondRequest;
	class AdvanceStepsRequest;
}

UCLASS()
class TEMPOTIME_API UTempoTimeServiceSubsystem : public UWorldSubsystem, public ITempoWorldScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;

private:
	void SetTimeMode(const TempoTime::TimeModeRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void SetSimStepsPerSecond(const TempoTime::SetSimStepsPerSecondRequest&, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void AdvanceSteps(const TempoTime::AdvanceStepsRequest&, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
	
	void Play(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void Pause(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void Step(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
};
