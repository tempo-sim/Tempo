// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsSettings.h"

UTempoSensorsSettings::UTempoSensorsSettings()
{
	CategoryName = TEXT("Tempo");
}

void UTempoSensorsSettings::PostInitProperties()
{
	Super::PostInitProperties();

	auto SetIfNull = [](TSoftObjectPtr<UMaterialInterface>& Ptr, const TCHAR* Path)
	{
		if (Ptr.IsNull())
		{
			Ptr = FSoftObjectPath(Path);
		}
	};

	SetIfNull(CameraPostProcessMaterialNoDepth, TEXT("/TempoSensors/Materials/M_TempoCamera_Distort_NoDepth.M_TempoCamera_Distort_NoDepth"));
	SetIfNull(CameraPostProcessMaterialWithDepth, TEXT("/TempoSensors/Materials/M_TempoCamera_Distort_WithDepth.M_TempoCamera_Distort_WithDepth"));
	SetIfNull(CameraStitchPassthroughMaterial, TEXT("/TempoSensors/Materials/M_TempoStitch_Passthrough.M_TempoStitch_Passthrough"));
	SetIfNull(CameraStitchAuxMaterial, TEXT("/TempoSensors/Materials/M_TempoStitch_Aux.M_TempoStitch_Aux"));
	SetIfNull(CameraStitchColorFeatherMaterial, TEXT("/TempoSensors/Materials/M_TempoStitch_ColorFeather.M_TempoStitch_ColorFeather"));
	SetIfNull(CameraStitchMergeMaterialWithDepth, TEXT("/TempoSensors/Materials/M_TempoStitch_Merge_WithDepth.M_TempoStitch_Merge_WithDepth"));
	SetIfNull(CameraStitchMergeMaterialNoDepth, TEXT("/TempoSensors/Materials/M_TempoStitch_Merge_NoDepth.M_TempoStitch_Merge_NoDepth"));
	SetIfNull(CameraProxyTonemapMaterial, TEXT("/TempoSensors/Materials/M_TempoStitch_Proxy.M_TempoStitch_Proxy"));
	SetIfNull(LidarPostProcessMaterial, TEXT("/TempoSensors/Materials/M_LidarPostProcess.M_LidarPostProcess"));
}

#if WITH_EDITOR
FText UTempoSensorsSettings::GetSectionText() const
{
	return FText::FromString(FString(TEXT("Sensors")));
}
#endif

#if WITH_EDITOR
void UTempoSensorsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FString PropertyChangedName = PropertyChangedEvent.Property->GetName();
	if (PropertyChangedName == GET_MEMBER_NAME_CHECKED(UTempoSensorsSettings, LabelType))
	{
		TempoSensorsLabelSettingsChangedEvent.Broadcast();
	}
}
#endif
