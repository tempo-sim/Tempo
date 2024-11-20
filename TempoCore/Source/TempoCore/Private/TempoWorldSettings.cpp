// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSettings.h"

#include "TempoCore.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
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
