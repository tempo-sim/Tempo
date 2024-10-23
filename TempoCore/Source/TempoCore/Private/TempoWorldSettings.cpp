// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSettings.h"

#include "TempoCore.h"
#include "Engine/DirectionalLight.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// These values result in relatively consistent perceived brightness in the range of ~5-100 Lux of scene intensity.
	constexpr float ExposureBiasToBrightnessScale = 30.0 / 150.0;
	constexpr float ExposureBiasToBrightnessOffset = -15.0;
}

void ATempoWorldSettings::SetDefaultAutoExposureBias()
{
	check(GetWorld());
	if (const ADirectionalLight* DirectionalLight = Cast<ADirectionalLight>(UGameplayStatics::GetActorOfClass(GetWorld(), ADirectionalLight::StaticClass())))
	{
		float SceneBrightness = DirectionalLight->GetBrightness();
		DefaultAutoExposureBias = ExposureBiasToBrightnessScale * SceneBrightness + ExposureBiasToBrightnessOffset;
	}
	else
	{
		UE_LOG(LogTempoCore, Error, TEXT("No directional light found in level. Cannot set default exposure compensation automatically."));
	}
}

void ATempoWorldSettings::BeginPlay()
{
	Super::BeginPlay();

	if (!bManualDefaultAutoExposureBias)
	{
		SetDefaultAutoExposureBias();
	}

	bHasBegunPlay = true;
}

float ATempoWorldSettings::GetDefaultAutoExposureBias()
{
	if (!bHasBegunPlay && !bManualDefaultAutoExposureBias)
	{
		SetDefaultAutoExposureBias();
	}
	return DefaultAutoExposureBias;
}

#if WITH_EDITOR
void ATempoWorldSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ATempoWorldSettings, bManualDefaultAutoExposureBias))
	{
		if (!bManualDefaultAutoExposureBias)
		{
			SetDefaultAutoExposureBias();
		}
	}
}
#endif
