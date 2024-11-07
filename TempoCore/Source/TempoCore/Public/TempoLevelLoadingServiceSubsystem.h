// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoLevelLoadingServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoCore
{
	class LoadLevelRequest;
}

UCLASS()
class TEMPOCORE_API UTempoLevelLoadingServiceSubsystem : public UTempoGameInstanceSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void LoadLevel(const TempoCore::LoadLevelRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void FinishLoadingLevel(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void SetDeferBeginPlay(bool bDeferBeginPlayIn) { bDeferBeginPlay = bDeferBeginPlayIn; }

	void SetStartPaused(bool bStartPausedIn) { bStartPaused = bStartPausedIn; }

	bool GetDeferBeginPlay() const { return bDeferBeginPlay; }

	bool GetStartPaused() const { return bStartPaused; }

protected:
	bool bDeferBeginPlay = false;

	bool bStartPaused = false;
};
