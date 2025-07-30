// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTempoSensors, Log, All);

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

	FDelegateHandle EngineInitCompleteHandle;
	TWeakPtr<SNotificationItem> ConfirmationNotificationPtr;
};
