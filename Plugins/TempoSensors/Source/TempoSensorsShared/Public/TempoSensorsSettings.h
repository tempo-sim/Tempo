// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "TempoSensorsSettings.generated.h"

UCLASS(Config=Game)
class TEMPOSENSORSSHARED_API UTempoSensorsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Labels
	TObjectPtr<UDataTable> GetSemanticLabelTable() const { return SemanticLabelTable.Get(); }

	// Camera
	int32 GetMaxCameraRenderBufferSize() const { return MaxCameraRenderBufferSize; }

private:
	UPROPERTY(EditAnywhere, Config, Category="Labels")
	TSoftObjectPtr<UDataTable> SemanticLabelTable;

	// The max number of frames per camera to buffer before dropping.
	UPROPERTY(EditAnywhere, Config, Category="Camera", AdvancedDisplay)
	int32 MaxCameraRenderBufferSize = 2;
};
