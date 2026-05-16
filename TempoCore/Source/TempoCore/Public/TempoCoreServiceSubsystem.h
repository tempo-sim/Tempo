// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoCoreServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
}

namespace TempoCore
{
	class LoadLevelRequest;
	class CurrentLevelResponse;
	class GetAvailableLevelsRequest;
	class AvailableLevelsResponse;
	class SetMainViewportRenderEnabledRequest;
	class SetControlModeRequest;
}

UCLASS()
class TEMPOCORE_API UTempoCoreServiceSubsystem : public UTempoGameInstanceSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void LoadLevel(const TempoCore::LoadLevelRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void FinishLoadingLevel(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void GetCurrentLevelName(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::CurrentLevelResponse>& ResponseContinuation);

	void GetAvailableLevels(const TempoCore::GetAvailableLevelsRequest& Request, const TResponseDelegate<TempoCore::AvailableLevelsResponse>& ResponseContinuation) const;

	void Quit(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void SetDeferBeginPlay(bool bDeferBeginPlayIn) { bDeferBeginPlay = bDeferBeginPlayIn; }

	void SetStartPaused(bool bStartPausedIn) { bStartPaused = bStartPausedIn; }

	bool GetDeferBeginPlay() const { return bDeferBeginPlay; }

	bool GetStartPaused() const { return bStartPaused; }

	void OnLevelLoaded();

	void SetRenderMainViewportEnabled(const TempoCore::SetMainViewportRenderEnabledRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

	void SetControlMode(const TempoCore::SetControlModeRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation);

protected:
	TOptional<TResponseDelegate<TempoCore::Empty>> PendingLevelLoad;

	bool bDeferBeginPlay = false;

	bool bStartPaused = false;
};
