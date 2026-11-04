// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTempoSensors, Log, All);

class FTempoGPUFrameThrottle;

class FTempoSensorsModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnEngineInitComplete();
	void ValidateSettings();
	void AutoFixSettings();
	void ShowConfirmationNotification(const TArray<FString>& SettingsToFix);
	void ShowSuccessNotification(const TArray<FString>& FixedSettings);
	void OnRenderSettingsChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	// The GPU frame throttle lives for the process (see FTempoGPUFrameThrottle): started with the
	// module, fed the settings once per game-thread frame, run at the end of every render-thread
	// frame, and stopped on the render thread when the module shuts down.
	void StartGPUFrameThrottle();
	void StopGPUFrameThrottle();
	void OnBeginFrame();

	FDelegateHandle EngineInitCompleteHandle;
	TWeakPtr<SNotificationItem> ConfirmationNotificationPtr;

	TSharedPtr<FTempoGPUFrameThrottle, ESPMode::ThreadSafe> GPUFrameThrottle;
	FDelegateHandle BeginFrameHandle;
	FDelegateHandle EndFrameRTHandle;
	// The settings last handed to the throttle, so the consistency check runs when they change and
	// not every frame.
	bool bLastLimitGPUFramesInFlight = false;
	int32 LastMaxGPUFramesInFlight = -1;
};
