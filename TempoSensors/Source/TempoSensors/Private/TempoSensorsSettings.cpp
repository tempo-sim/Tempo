// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsSettings.h"

#include "Engine/DataTable.h"

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
	LidarPostProcessMaterialWithColor = FSoftObjectPath(TEXT("/TempoSensors/Materials/M_LidarPostProcess_WithColor.M_LidarPostProcess_WithColor"));
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
	SetIfNull(LidarPostProcessMaterialWithColor, TEXT("/TempoSensors/Materials/M_LidarPostProcess_WithColor.M_LidarPostProcess_WithColor"));
}

void UTempoSensorsSettings::SetRuntimeSemanticLabelTable(UDataTable* SemanticLabelTableIn)
{
	if (RuntimeSemanticLabelTable == SemanticLabelTableIn)
	{
		return;
	}

	RuntimeSemanticLabelTable = SemanticLabelTableIn;

	TempoSensorsLabelSettingsChangedEvent.Broadcast();
	// The overridable/overriding row names are unchanged, but the rows they name now live in a
	// different table and may resolve to different label IDs.
	TempoSensorsLabelOverridesChangedEvent.Broadcast();
}

void UTempoSensorsSettings::SetLabelType(ELabelType LabelTypeIn)
{
	if (LabelType == LabelTypeIn)
	{
		return;
	}

	LabelType = LabelTypeIn;

	TempoSensorsLabelSettingsChangedEvent.Broadcast();
}

void UTempoSensorsSettings::SetGloballyUniqueInstanceLabels(bool bGloballyUniqueInstanceLabelsIn)
{
	// Read live by the instance ID allocator, so this only governs allocations from here on.
	// Instance IDs already assigned keep theirs.
	bGloballyUniqueInstanceLabels = bGloballyUniqueInstanceLabelsIn;
}

void UTempoSensorsSettings::SetInstantaneouslyUniqueInstanceLabels(bool bInstantaneouslyUniqueInstanceLabelsIn)
{
	bInstantaneouslyUniqueInstanceLabels = bInstantaneouslyUniqueInstanceLabelsIn;
}

void UTempoSensorsSettings::SetLabelRowNameOverrides(FName OverridableLabelRowNameIn, FName OverridingLabelRowNameIn)
{
	if (OverridableLabelRowName == OverridableLabelRowNameIn && OverridingLabelRowName == OverridingLabelRowNameIn)
	{
		return;
	}

	OverridableLabelRowName = OverridableLabelRowNameIn;
	OverridingLabelRowName = OverridingLabelRowNameIn;

	TempoSensorsLabelOverridesChangedEvent.Broadcast();
}

void UTempoSensorsSettings::SetPipelinedRendering(bool bPipelinedRenderingIn)
{
	// Read live everywhere it matters (the FixedStep readback barrier), so no listeners to notify.
	bPipelinedRendering = bPipelinedRenderingIn;
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

	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	const FName PropertyChangedName = PropertyChangedEvent.Property->GetFName();
	if (PropertyChangedName == GET_MEMBER_NAME_CHECKED(UTempoSensorsSettings, LabelType)
		|| PropertyChangedName == GET_MEMBER_NAME_CHECKED(UTempoSensorsSettings, SemanticLabelTable))
	{
		TempoSensorsLabelSettingsChangedEvent.Broadcast();
	}
	if (PropertyChangedName == GET_MEMBER_NAME_CHECKED(UTempoSensorsSettings, SemanticLabelTable)
		|| PropertyChangedName == GET_MEMBER_NAME_CHECKED(UTempoSensorsSettings, OverridableLabelRowName)
		|| PropertyChangedName == GET_MEMBER_NAME_CHECKED(UTempoSensorsSettings, OverridingLabelRowName))
	{
		TempoSensorsLabelOverridesChangedEvent.Broadcast();
	}
}
#endif
