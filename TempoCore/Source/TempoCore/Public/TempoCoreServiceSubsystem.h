// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoCoreServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoCore
{
	class LoadLevelRequest;
	class CurrentLevelResponse;
	class SetMainViewportRenderEnabledRequest;
	class SetControlModeRequest;
}

UCLASS()
class TEMPOCORE_API UTempoCoreServiceSubsystem : public UTempoGameInstanceSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void LoadLevel(const TempoCore::LoadLevelRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void FinishLoadingLevel(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void GetCurrentLevelName(const TempoScripting::Empty& Request, const TResponseDelegate<TempoCore::CurrentLevelResponse>& ResponseContinuation);

	void Quit(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void SetDeferBeginPlay(bool bDeferBeginPlayIn) { bDeferBeginPlay = bDeferBeginPlayIn; }

	void SetStartPaused(bool bStartPausedIn) { bStartPaused = bStartPausedIn; }

	bool GetDeferBeginPlay() const { return bDeferBeginPlay; }

	bool GetStartPaused() const { return bStartPaused; }

	void OnLevelLoaded();

	void SetRenderMainViewportEnabled(const TempoCore::SetMainViewportRenderEnabledRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void SetControlMode(const TempoCore::SetControlModeRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

protected:
	TOptional<TResponseDelegate<TempoScripting::Empty>> PendingLevelLoad;

	bool bDeferBeginPlay = false;

	bool bStartPaused = false;
};
