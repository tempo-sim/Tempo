// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsSettings.h"

UTempoSensorsSettings::UTempoSensorsSettings()
{
	CategoryName = TEXT("Tempo");

	// Set default soft-asset paths in the constructor so the CDO carries them as its true
	// defaults — that's what Project Settings' "Reset to Default" snapshots and what
	// SaveConfig elides against. PostInitProperties below is a backstop for projects whose
	// DefaultPlugins.ini has stale `=None` entries that would otherwise null these out at
	// LoadConfig time.
	CameraPostProcessMaterialNoDepth = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_TempoCamera_Distort_NoDepth.M_TempoCamera_Distort_NoDepth"));
	CameraPostProcessMaterialWithDepth = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_TempoCamera_Distort_WithDepth.M_TempoCamera_Distort_WithDepth"));
	CameraStitchAuxMaterial = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_TempoStitch_Aux.M_TempoStitch_Aux"));
	CameraStitchColorFeatherMaterial = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_TempoStitch_ColorFeather.M_TempoStitch_ColorFeather"));
	CameraStitchMergeMaterialWithDepth = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_TempoStitch_Merge_WithDepth.M_TempoStitch_Merge_WithDepth"));
	CameraStitchMergeMaterialNoDepth = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_TempoStitch_Merge_NoDepth.M_TempoStitch_Merge_NoDepth"));
	CameraProxyTonemapMaterial = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_TempoStitch_Proxy.M_TempoStitch_Proxy"));
	LidarPostProcessMaterial = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_LidarPostProcess.M_LidarPostProcess"));
}

void UTempoSensorsSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Restore defaults for any path that LoadConfig nulled out via a `=None` entry in
	// DefaultPlugins.ini. Constructor-set defaults handle the no-entry case; this handles
	// the explicit-None case (e.g., stale entries from older plugin versions).
	auto SetIfNull = [](TSoftObjectPtr<UMaterialInterface>& Ptr, const TCHAR* Path)
	{
		if (Ptr.IsNull())
		{
			Ptr = FSoftObjectPath(Path);
		}
	};

	SetIfNull(CameraPostProcessMaterialNoDepth, TEXT("/TempoSensors/Materials/M_TempoCamera_Distort_NoDepth.M_TempoCamera_Distort_NoDepth"));
	SetIfNull(CameraPostProcessMaterialWithDepth, TEXT("/TempoSensors/Materials/M_TempoCamera_Distort_WithDepth.M_TempoCamera_Distort_WithDepth"));
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
