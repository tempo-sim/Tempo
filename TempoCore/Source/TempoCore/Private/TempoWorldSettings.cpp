// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSettings.h"

#include "TempoCore.h"
#include "EngineUtils.h"
#include "TempoCoreSettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// These values result in relatively consistent perceived brightness across a huge range of scene intensity.
	constexpr float ExposureBiasToBrightnessScale = 3.93342962372;
	constexpr float ExposureBiasToBrightnessOffset = -34.1331407526;
}

void ATempoWorldSettings::SetDefaultAutoExposureBias()
{
	check(GetWorld());
	for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		if (const UDirectionalLightComponent* DirectionalLightComponent = Cast<UDirectionalLightComponent>(ActorIt->GetComponentByClass(UDirectionalLightComponent::StaticClass())))
		{
			const float SceneBrightness = DirectionalLightComponent->Intensity;
			DefaultAutoExposureBias = ExposureBiasToBrightnessScale * FMath::Log2(SceneBrightness) + ExposureBiasToBrightnessOffset;
			return;
		}
	}
	UE_LOG(LogTempoCore, Error, TEXT("No directional light found in level. Cannot set default exposure compensation automatically."));
}

void ATempoWorldSettings::BeginPlay()
{
	Super::BeginPlay();

	if (!bManualDefaultAutoExposureBias)
	{
		SetDefaultAutoExposureBias();
	}

	if (UTempoCoreSettings* TempoCoreSettings = GetMutableDefault<UTempoCoreSettings>())
	{
		SetMainViewportRenderEnabled(TempoCoreSettings->GetRenderMainViewport());
		RenderingSettingsChangedHandle = TempoCoreSettings->TempoCoreRenderingSettingsChanged.AddUObject(this, &ATempoWorldSettings::TempoCoreRenderSettingsChanged);
	}

	bHasBegunPlay = true;
}

void ATempoWorldSettings::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (UTempoCoreSettings* TempoCoreSettings = GetMutableDefault<UTempoCoreSettings>())
	{
		TempoCoreSettings->TempoCoreRenderingSettingsChanged.Remove(RenderingSettingsChangedHandle);
	}
}

void ATempoWorldSettings::TempoCoreRenderSettingsChanged() const
{
	if (const UTempoCoreSettings* TempoCoreSettings = GetDefault<UTempoCoreSettings>())
	{
		SetMainViewportRenderEnabled(TempoCoreSettings->GetRenderMainViewport());
	}
}

void ATempoWorldSettings::SetMainViewportRenderEnabled(bool bEnabled) const
{
	if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (ULocalPlayer* ClientPlayer = PlayerController->GetLocalPlayer())
		{
			if (UGameViewportClient* ViewportClient = ClientPlayer->ViewportClient)
			{
				ViewportClient->bDisableWorldRendering = !bEnabled;
			}
		}
	}
}

float ATempoWorldSettings::GetDefaultAutoExposureBias()
{
	if (!bHasBegunPlay && !bManualDefaultAutoExposureBias)
	{
		SetDefaultAutoExposureBias();
	}
	return DefaultAutoExposureBias;
}
