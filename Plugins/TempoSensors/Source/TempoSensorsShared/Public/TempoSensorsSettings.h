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
	TObjectPtr<UDataTable> GetSemanticLabelTable() const { return SemanticLabelTable.LoadSynchronous(); }

	// Camera
	TObjectPtr<UMaterialInterface> GetCameraPostProcessMaterialNoDepth() const { return CameraPostProcessMaterialNoDepth.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetCameraPostProcessMaterialWithDepth() const { return CameraPostProcessMaterialWithDepth.LoadSynchronous(); }
	float GetMaxCameraDepth() const { return MaxCameraDepth; }
	int32 GetMaxCameraRenderBufferSize() const { return MaxCameraRenderBufferSize; }

private:
	// The data table from which we will load the mapping from Actor classes to semantic labels.
	UPROPERTY(EditAnywhere, Config, Category="Labels")
	TSoftObjectPtr<UDataTable> SemanticLabelTable;

	// The post process material that should be used by TempoCamera .
	UPROPERTY(EditAnywhere, Config, Category="Camera", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> CameraPostProcessMaterialNoDepth;

	UPROPERTY(EditAnywhere, Config, Category="Camera", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> CameraPostProcessMaterialWithDepth;

	// The expected maximum required depth for a camera depth image.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	float MaxCameraDepth = 100000.0; // 1km
	
	// The max number of frames per camera to buffer before dropping.
	UPROPERTY(EditAnywhere, Config, Category="Camera", AdvancedDisplay)
	int32 MaxCameraRenderBufferSize = 2;
};
