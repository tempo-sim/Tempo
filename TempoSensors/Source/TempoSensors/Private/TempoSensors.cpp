// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensors.h"

#include "TempoGPUFrameThrottle.h"
#include "TempoSensorsSettings.h"

#include "DynamicRHI.h"
#include "Engine/RendererSettings.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FTempoSensorsModule"

DEFINE_LOG_CATEGORY(LogTempoSensors);

void FTempoSensorsModule::StartupModule()
{
	if (GIsEditor && !IsRunningCommandlet())
	{
		EngineInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FTempoSensorsModule::OnEngineInitComplete);
	}

	StartGPUFrameThrottle();
}

void FTempoSensorsModule::StartGPUFrameThrottle()
{
	GPUFrameThrottle = MakeShared<FTempoGPUFrameThrottle, ESPMode::ThreadSafe>();

	BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddRaw(this, &FTempoSensorsModule::OnBeginFrame);

	// Bound at module startup, ahead of any world subsystem's end-of-frame handler, so a frame's GPU
	// bound is enforced before the sensors poll their readbacks and the reads it guarantees complete
	// are picked up in the same callback rather than a frame later. The lambda shares ownership: the
	// throttle stays alive until the render thread drops the delegate in StopGPUFrameThrottle, however
	// the module's own pointer is handled.
	TSharedPtr<FTempoGPUFrameThrottle, ESPMode::ThreadSafe> Throttle = GPUFrameThrottle;
	EndFrameRTHandle = FCoreDelegates::OnEndFrameRT.AddLambda([Throttle]()
	{
		Throttle->OnEndFrameRT(FRHICommandListImmediate::Get());
	});
}

void FTempoSensorsModule::StopGPUFrameThrottle()
{
	if (!GPUFrameThrottle)
	{
		return;
	}

	FCoreDelegates::OnBeginFrame.Remove(BeginFrameHandle);

	// OnEndFrameRT is broadcast on the render thread and its list is not thread-safe, so the handler
	// is removed there, in order behind any broadcast already queued; the fences go with it. Then
	// wait, so the throttle is not reached after this returns.
	ENQUEUE_RENDER_COMMAND(TempoStopGPUFrameThrottle)(
		[Handle = EndFrameRTHandle, Throttle = GPUFrameThrottle](FRHICommandListImmediate&)
		{
			FCoreDelegates::OnEndFrameRT.Remove(Handle);
			Throttle->Release();
		});
	FlushRenderingCommands();
	GPUFrameThrottle.Reset();
}

void FTempoSensorsModule::OnBeginFrame()
{
	if (!GPUFrameThrottle || IsRunningCommandlet())
	{
		return;
	}

	const UTempoSensorsSettings* Settings = GetDefault<UTempoSensorsSettings>();
	const bool bLimit = Settings->GetLimitGPUFramesInFlight();
	// What the throttle will enforce: the setting's editor clamp does not apply to an ini value.
	const int32 Bound = FTempoGPUFrameThrottle::ClampBound(Settings->GetMaxGPUFramesInFlight());

	if (bLimit != bLastLimitGPUFramesInFlight || Bound != LastMaxGPUFramesInFlight)
	{
		bLastLimitGPUFramesInFlight = bLimit;
		LastMaxGPUFramesInFlight = Bound;

		// The bound only protects Vulkan's frame-counted recycling if that recycling waits longer than
		// the bound. The plugin's Config/Engine.ini sets the variable to fit the default bound; a
		// project that changes either one on its own gets a warning rather than a device loss.
		if (bLimit && GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::Vulkan)
		{
			if (const IConsoleVariable* RecycleDelay = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Vulkan.NumFramesToWaitForResourceDelete")))
			{
				if (Bound >= RecycleDelay->GetInt())
				{
					UE_LOG(LogTempoSensors, Warning,
						TEXT("MaxGPUFramesInFlight (%d as enforced) is not below r.Vulkan.NumFramesToWaitForResourceDelete (%d): the GPU may read uniform-buffer memory the RHI has already recycled (VK_ERROR_DEVICE_LOST). Raise the console variable in [ConsoleVariables] or lower the bound."),
						Bound, RecycleDelay->GetInt());
				}
			}
		}
	}

	GPUFrameThrottle->Configure(bLimit, Bound);
}

void FTempoSensorsModule::OnEngineInitComplete()
{
#if WITH_EDITOR
	URendererSettings* RenderSettings = GetMutableDefault<URendererSettings>();
	RenderSettings->OnSettingChanged().AddRaw(this, &FTempoSensorsModule::OnRenderSettingsChanged);
#endif

	ValidateSettings();
}

void FTempoSensorsModule::OnRenderSettingsChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	ValidateSettings();
}

void FTempoSensorsModule::ValidateSettings()
{
	// Dismiss any previous notification
	if (ConfirmationNotificationPtr.IsValid())
	{
		ConfirmationNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_None);
		ConfirmationNotificationPtr.Pin()->ExpireAndFadeout();
	}

	TArray<FString> SettingsToFix;
	const URendererSettings* RenderSettings = GetDefault<URendererSettings>();

	if (RenderSettings->CustomDepthStencil != ECustomDepthStencil::EnabledWithStencil)
	{
		SettingsToFix.Add(TEXT("Change r.CustomDepth to EnabledWithStencil"));
	}

	if (SettingsToFix.IsEmpty())
	{
		// Nothing to fix
		return;
	}

	ShowConfirmationNotification(SettingsToFix);
}

void FTempoSensorsModule::ShowConfirmationNotification(const TArray<FString>& SettingsToFix)
{
	FString NotificationText = TEXT("Apply recommended TempoSensors settings?");

	FString SubText = TEXT("The following settings will be changed:\n");
	for (int32 i = 0; i < FMath::Min(SettingsToFix.Num(), 3); ++i)
	{
		SubText += TEXT("• ") + SettingsToFix[i];
		if (i < FMath::Min(SettingsToFix.Num(), 3) - 1)
		{
			SubText += TEXT("\n");
		}
	}

	FNotificationInfo Info(FText::FromString(NotificationText));
	Info.SubText = FText::FromString(SubText);
	Info.bFireAndForget = false;
	Info.FadeOutDuration = 0.0f;
	Info.ExpireDuration = 0.0f;
	Info.bUseThrobber = false;
	Info.bUseSuccessFailIcons = false;

	// Alternative: Try the button approach with different completion state
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		FText::FromString(TEXT("Apply")),
		FText::FromString(TEXT("Apply changes")),
		FSimpleDelegate::CreateRaw(this, &FTempoSensorsModule::AutoFixSettings),
		SNotificationItem::CS_Pending
	));

	Info.ButtonDetails.Add(FNotificationButtonInfo(
		FText::FromString(TEXT("Cancel")),
		FText::FromString(TEXT("Cancel")),
		FSimpleDelegate::CreateLambda([this]()
		{
			if (ConfirmationNotificationPtr.IsValid())
			{
				ConfirmationNotificationPtr.Pin()->ExpireAndFadeout();
			}
		}),
		SNotificationItem::CS_Pending
	));

	ConfirmationNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);

	if (ConfirmationNotificationPtr.IsValid())
	{
		ConfirmationNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FTempoSensorsModule::AutoFixSettings()
{
	bool bSettingsChanged = false;
	TArray<FString> FixedSettings;

	URendererSettings* RenderSettings = GetMutableDefault<URendererSettings>();

	if (RenderSettings->CustomDepthStencil != ECustomDepthStencil::EnabledWithStencil)
	{
		RenderSettings->CustomDepthStencil = ECustomDepthStencil::EnabledWithStencil;
		FixedSettings.Add(TEXT("Changed r.CustomDepth to EnabledWithStencil"));
		bSettingsChanged = true;
	}

	if (bSettingsChanged)
	{
		RenderSettings->SaveConfig();
		RenderSettings->UpdateSinglePropertyInConfigFile(
			RenderSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(URendererSettings, CustomDepthStencil)),
				RenderSettings->GetDefaultConfigFilename());

		ShowSuccessNotification(FixedSettings);
	}

	if (ConfirmationNotificationPtr.IsValid())
	{
		ConfirmationNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_None);
		ConfirmationNotificationPtr.Pin()->ExpireAndFadeout();
	}
}

void FTempoSensorsModule::ShowSuccessNotification(const TArray<FString>& FixedSettings)
{
	FString NotificationText = FString::Printf(TEXT("Applied recommended project settings for TempoSensors:"));

	FString SubText;
	for (int32 i = 0; i < FMath::Min(FixedSettings.Num(), 3); ++i)
	{
		SubText += FixedSettings[i];
		if (i < FMath::Min(FixedSettings.Num(), 3) - 1)
		{
			SubText += TEXT("\n");
		}
	}

	FNotificationInfo Info(FText::FromString(NotificationText));
	Info.SubText = FText::FromString(SubText);
	Info.bFireAndForget = true; // Auto-dismiss success notifications
	Info.FadeOutDuration = 3.0f;
	Info.ExpireDuration = 5.0f;
	Info.bUseThrobber = false;
	Info.bUseSuccessFailIcons = false;
	Info.Image = FCoreStyle::Get().GetBrush(TEXT("NotificationList.SuccessImage")); // Success icon

	FSlateNotificationManager::Get().AddNotification(Info);
}

void FTempoSensorsModule::ShutdownModule()
{
	StopGPUFrameThrottle();

	FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTempoSensorsModule, TempoSensors)
