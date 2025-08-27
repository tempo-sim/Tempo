// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSensorsTypes.h"

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "TempoSensorsSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FTempoSensorsLabelSettingsChanged);

/**
 * TempoSensors Plugin Settings.
 */
UCLASS(Config=Plugins, DefaultConfig)
class TEMPOSENSORSSHARED_API UTempoSensorsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTempoSensorsSettings();

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif

	// Labels
	TObjectPtr<UDataTable> GetSemanticLabelTable() const { return SemanticLabelTable.LoadSynchronous(); }
	ELabelType GetLabelType() const { return LabelType; }
	bool GetGloballyUniqueInstanceLabels() const { return bGloballyUniqueInstanceLabels; }
	bool GetInstantaneouslyUniqueInstanceLabels() const { return bInstantaneouslyUniqueInstanceLabels; }

	// Camera
	TObjectPtr<UMaterialInterface> GetCameraPostProcessMaterialNoDepth() const { return CameraPostProcessMaterialNoDepth.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetCameraPostProcessMaterialWithDepth() const { return CameraPostProcessMaterialWithDepth.LoadSynchronous(); }
	EColorImageEncoding GetColorImageEncoding() const { return ColorImageEncoding; }
	float GetMaxCameraDepth() const { return MaxCameraDepth; }
	float GetSceneCaptureGamma() const { return SceneCaptureGamma; }
	FName GetOverridableLabelRowName() const { return OverridableLabelRowName; }
	FName GetOverridingLabelRowName() const { return OverridingLabelRowName; }
	int32 GetMaxCameraRenderBufferSize() const { return MaxCameraRenderBufferSize; }
	FTempoSensorsLabelSettingsChanged TempoSensorsLabelSettingsChangedEvent;

	// Lidar
	int32 GetLidarUpsamplingFactor() const { return LidarUpsamplingFactor; }
	float GetMaxLidarDepth() const { return MaxLidarDepth; }
	TObjectPtr<UMaterialInterface> GetLidarPostProcessMaterial() const { return LidarPostProcessMaterial.LoadSynchronous(); }

	// RayTracingScene Buffer Overrun Workaround
	bool GetRayTracingSceneReadbackBuffersOverrunWorkaroundEnabled() const { return bEnableRayTracingSceneReadbackBuffersOverrunWorkaround; }
	uint32 GetRayTracingSceneMaxReadbackBuffersOverride() const { return RayTracingSceneMaxReadbackBuffersOverride; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// The data table from which we will load the mapping from Actor classes or static meshes to semantic labels.
	// In Instance label mode we will label all instances of objects that appear in the semantic label table.
	UPROPERTY(EditAnywhere, Config, Category="Labels")
	TSoftObjectPtr<UDataTable> SemanticLabelTable;

	// The global label type to use.
	UPROPERTY(EditAnywhere, Config, Category="Labels")
	TEnumAsByte<ELabelType> LabelType = ELabelType::Semantic;

	// Whether to reclaim instance labels for later use when labeled objects are destroyed.
	UPROPERTY(EditAnywhere, Config, Category="Labels", meta=(EditCondition="LabelType == ELabelType::Instance"))
	bool bGloballyUniqueInstanceLabels = false;

	// Whether to reuse instance labels after exhausting our 256 unique labels.
	UPROPERTY(EditAnywhere, Config, Category="Labels", meta=(EditCondition="LabelType == ELabelType::Instance"))
	bool bInstantaneouslyUniqueInstanceLabels = false;

	// The post process material that should be used by TempoCamera when not capturing the depth image.
	UPROPERTY(EditAnywhere, Config, Category="Camera", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> CameraPostProcessMaterialNoDepth;

	// The post process material that should be used by TempoCamera when capturing the depth image.
	UPROPERTY(EditAnywhere, Config, Category="Camera", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> CameraPostProcessMaterialWithDepth;

	// Encoding to use for color images.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	TEnumAsByte<EColorImageEncoding> ColorImageEncoding = EColorImageEncoding::BGR8;

	// The expected maximum required depth for a camera depth image.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	float MaxCameraDepth = 100000.0; // 1km

	// Gamma to use for simulated scene captures.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	float SceneCaptureGamma = 2.2;
	
	// The max number of frames per camera to buffer before dropping.
	UPROPERTY(EditAnywhere, Config, Category="Camera", AdvancedDisplay)
	int32 MaxCameraRenderBufferSize = 2;

	// This special row can be overriden by a value passed through the subsurface color.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	FName OverridableLabelRowName = NAME_None;

	// Anywhere a non-zero subsurface color is found on an object of type OverridableLabelRowName, this label will be used instead.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	FName OverridingLabelRowName = NAME_None;

	// The upsampling factor used to scale the number of horizontal pixels in the Lidar depth image relative to the number
	// of horizontal beams. Higher factors improve the accuracy of the reconstruction at the cost of performance.
	UPROPERTY(EditAnywhere, Config, Category="Lidar", meta=(UIMin=1, UIMax=4, ClampMin=1, ClampMax=4))
	int32 LidarUpsamplingFactor = 2;

	// The expected maximum required depth for a Lidar return.
	UPROPERTY(EditAnywhere, Config, Category="Lidar")
	float MaxLidarDepth = 40000.0; // 400m

	UPROPERTY(EditAnywhere, Config, Category="Lidar", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> LidarPostProcessMaterial;

	// Whether to enable a hack to work around a buffer overrun bug in FRayTracingScene.
	UPROPERTY(EditAnywhere, Config, Category="Advanced")
	bool bEnableRayTracingSceneReadbackBuffersOverrunWorkaround = true;

	// The size of buffer to use as an override in FRayTracingScene, if enabled.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=(EditCondition=bEnableRayTracingSceneReadbackBuffersOverrunWorkaround))
	uint32 RayTracingSceneMaxReadbackBuffersOverride = 40;
};
