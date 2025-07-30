// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensors.h"

#include "Engine/RendererSettings.h"

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
    FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTempoSensorsModule, TempoSensors)
