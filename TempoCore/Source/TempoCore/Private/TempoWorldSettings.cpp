// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSettings.h"

#include "TempoCore.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// These values result in relatively consistent perceived brightness in the range of ~5-100 Lux of scene intensity.
	constexpr float ExposureBiasToBrightnessScale = 0.17894736842;
	constexpr float ExposureBiasToBrightnessOffset = -25.8947368421;
}

void ATempoWorldSettings::SetDefaultAutoExposureBias()
{
	check(GetWorld());
	for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		if (const UDirectionalLightComponent* DirectionalLightComponent = Cast<UDirectionalLightComponent>(ActorIt->GetComponentByClass(UDirectionalLightComponent::StaticClass())))
		{
			const float SceneBrightness = DirectionalLightComponent->Intensity;
			DefaultAutoExposureBias = ExposureBiasToBrightnessScale * SceneBrightness + ExposureBiasToBrightnessOffset;
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
