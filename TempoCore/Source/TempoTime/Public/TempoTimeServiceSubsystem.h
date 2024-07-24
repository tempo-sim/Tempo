// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoWorldSubsystem.h"

#include "TempoScripting/Empty.pb.h"

#include "CoreMinimal.h"

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
class TEMPOTIME_API UTempoTimeServiceSubsystem : public UTempoWorldSubsystem, public ITempoWorldScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;

protected:
	void SetTimeMode(const TempoTime::TimeModeRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void SetSimStepsPerSecond(const TempoTime::SetSimStepsPerSecondRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void AdvanceSteps(const TempoTime::AdvanceStepsRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
	
	void Play(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void Pause(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void Step(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
};
