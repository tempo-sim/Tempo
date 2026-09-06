// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsUtils.h"

#include "TempoLabelTypes.h"
#include "TempoSensorsSettings.h"

#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void OptimizeShowFlagsForNoColor(FEngineShowFlags& ShowFlags)
{
	ShowFlags.SetPostProcessing(true);
	ShowFlags.SetPostProcessMaterial(true);
	ShowFlags.SetHighResScreenshotMask(false);
	ShowFlags.SetHMDDistortion(false);
	ShowFlags.SetStereoRendering(false);
	ShowFlags.SetLocalExposure(false);
	ShowFlags.SetTonemapper(false);
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetAmbientCubemap(false);
	ShowFlags.SetEyeAdaptation(false);
	ShowFlags.SetLensFlares(false);
	ShowFlags.SetBloom(false);
	ShowFlags.SetGlobalIllumination(false);
	ShowFlags.SetVignette(false);
	ShowFlags.SetGrain(false);
	ShowFlags.SetAmbientOcclusion(false);
	ShowFlags.SetCameraImperfections(false);
	ShowFlags.SetLighting(false);
	ShowFlags.SetDirectLighting(false);
	ShowFlags.SetDirectionalLights(false);
	ShowFlags.SetPointLights(false);
	ShowFlags.SetRectLights(false);
	ShowFlags.SetColorGrading(false);
	ShowFlags.SetDepthOfField(false);
	ShowFlags.SetMotionBlur(false);
	ShowFlags.SetRefraction(false);
	ShowFlags.SetSceneColorFringe(false);
	ShowFlags.SetCameraInterpolation(false);
	ShowFlags.SetToneCurve(false);
	ShowFlags.SetSeparateTranslucency(false);
	ShowFlags.SetReflectionEnvironment(false);
	ShowFlags.SetDecals(false);
	// ShowFlags.SetHairStrands(false);
	ShowFlags.SetDiffuse(false);
	ShowFlags.SetSpecular(false);
	ShowFlags.SetScreenSpaceReflections(false);
	ShowFlags.SetLumenReflections(false);
	ShowFlags.SetContactShadows(false);
	ShowFlags.SetRayTracedDistanceFieldShadows(false);
	ShowFlags.SetCapsuleShadows(false);
	ShowFlags.SetSubsurfaceScattering(false);
	ShowFlags.SetVolumetricLightmap(false);
	ShowFlags.SetIndirectLightingCache(false);
	ShowFlags.SetTexturedLightProfiles(false);
	ShowFlags.SetLightFunctions(false);
	ShowFlags.SetDynamicShadows(false);
	ShowFlags.SetTranslucency(false);
	ShowFlags.SetDeferredLighting(false);
	ShowFlags.SetLightShafts(false);
	ShowFlags.SetAtmosphere(false);
	ShowFlags.SetCloud(false);
	ShowFlags.SetScreenSpaceAO(false);
	ShowFlags.SetDistanceFieldAO(false);
	ShowFlags.SetLumenGlobalIllumination(false);
	ShowFlags.SetVolumetricFog(false);
	ShowFlags.SetFog(false);
	ShowFlags.SetShaderPrint(false);
	ShowFlags.SetVirtualShadowMapPersistentData(false);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	ShowFlags.SetLensDistortion(false);
	ShowFlags.SetMegaLights(false);
#endif
}

void ApplyPhotorealisticRenderSettings(FPostProcessSettings& OutPostProcess,
	FEngineShowFlags& OutShowFlags, bool& OutUseRayTracingIfEnabled)
{
	// Auto exposure: AEM_Histogram samples the (PPM-replaced, for the camera; raw scene, for the
	// lidar) scene color histogram to compute exposure. The aggressive speed-up/down keeps the
	// exposure responsive when the scene brightness shifts rapidly between captures.
	OutPostProcess.bOverride_AutoExposureMethod = true;
	OutPostProcess.AutoExposureMethod = AEM_Histogram;
	OutPostProcess.bOverride_AutoExposureSpeedUp = true;
	OutPostProcess.AutoExposureSpeedUp = 20.0;
	OutPostProcess.bOverride_AutoExposureSpeedDown = true;
	OutPostProcess.AutoExposureSpeedDown = 20.0;
	OutPostProcess.bOverride_AutoExposureLowPercent = true;
	OutPostProcess.AutoExposureLowPercent = 75.0;
	OutPostProcess.bOverride_AutoExposureHighPercent = true;
	OutPostProcess.AutoExposureHighPercent = 85.0;

	// Lumen
	OutPostProcess.bOverride_DynamicGlobalIlluminationMethod = true;
	OutPostProcess.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	OutPostProcess.bOverride_ReflectionMethod = true;
	OutPostProcess.ReflectionMethod = EReflectionMethod::Lumen;

	// Lumen's screen probe gather accumulates its history in frames, not seconds, so at sensor
	// rates a mover can trail its indirect lighting for a long time. FinalGatherQuality scales the
	// rays traced per probe (by its square root), lowering the per-frame noise the accumulation
	// exists to hide. LightingUpdateSpeed divides the frames accumulated (by its square root, so the
	// engine's 10 becomes 5) and raises the radiance cache's per-frame trace budget, so lighting
	// changes propagate faster. ReflectionQuality scales the reflection denoiser's spatial
	// reconstruction sample count only; the reflection history length is set by
	// r.Lumen.Reflections.Temporal.MaxFramesAccumulated.
	OutPostProcess.bOverride_LumenFinalGatherQuality = true;
	OutPostProcess.LumenFinalGatherQuality = 2.0f;
	OutPostProcess.bOverride_LumenFinalGatherLightingUpdateSpeed = true;
	OutPostProcess.LumenFinalGatherLightingUpdateSpeed = 4.0f;
	OutPostProcess.bOverride_LumenReflectionQuality = true;
	OutPostProcess.LumenReflectionQuality = 2.0f;

	// Megalights
	OutPostProcess.bOverride_bMegaLights = true;
	OutPostProcess.bMegaLights = true;

	OutUseRayTracingIfEnabled = true;

	OutShowFlags.SetMotionBlur(false);
	OutShowFlags.SetAntiAliasing(true);
	OutShowFlags.SetTemporalAA(true);
	OutShowFlags.SetEyeAdaptation(true);
	OutShowFlags.SetLocalExposure(true);
	OutShowFlags.SetLensFlares(true);
	OutShowFlags.SetBloom(true);
	OutShowFlags.SetColorGrading(true);
	OutShowFlags.SetVignette(true);
	OutShowFlags.SetDepthOfField(true);
	OutShowFlags.SetGlobalIllumination(true);
	OutShowFlags.SetScreenSpaceReflections(true);
	OutShowFlags.SetReflectionEnvironment(true);
	OutShowFlags.SetAmbientOcclusion(true);
	OutShowFlags.SetScreenSpaceAO(true);
	OutShowFlags.SetDistanceFieldAO(true);
	OutShowFlags.SetVolumetricFog(true);
	OutShowFlags.SetTonemapper(true);
	OutShowFlags.SetScreenPercentage(true);
}

void ApplyLabelOverrideParameters(UMaterialInstanceDynamic* MaterialInstance)
{
	if (!MaterialInstance)
	{
		return;
	}

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	const UDataTable* SemanticLabelTable = TempoSensorsSettings->GetSemanticLabelTable();
	const FName OverridableLabelRowName = TempoSensorsSettings->GetOverridableLabelRowName();
	const FName OverridingLabelRowName = TempoSensorsSettings->GetOverridingLabelRowName();
	TOptional<int32> OverridableLabel;
	TOptional<int32> OverridingLabel;
	if (SemanticLabelTable && !OverridableLabelRowName.IsNone())
	{
		SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""),
			[&OverridableLabelRowName, &OverridingLabelRowName, &OverridableLabel, &OverridingLabel]
			(const FName& Key, const FSemanticLabel& Value)
			{
				if (Key == OverridableLabelRowName)
				{
					OverridableLabel = Value.Label;
				}
				if (Key == OverridingLabelRowName)
				{
					OverridingLabel = Value.Label;
				}
			});
	}

	if (OverridableLabel.IsSet() && OverridingLabel.IsSet())
	{
		MaterialInstance->SetScalarParameterValue(TEXT("OverridableLabel"), OverridableLabel.GetValue());
		MaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), OverridingLabel.GetValue());
	}
	else
	{
		MaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), 0.0);
	}
}

namespace
{
	// Write a row's TSet column as a sorted JSON array. Sorting is what makes two exports of the
	// same table comparable; TSet iteration order is not stable.
	void WriteSortedStringArray(const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>>& JsonWriter,
		const TCHAR* ColumnName, TArray<FString>& Values)
	{
		Values.Sort();
		JsonWriter->WriteArrayStart(ColumnName);
		for (const FString& Value : Values)
		{
			JsonWriter->WriteValue(Value);
		}
		JsonWriter->WriteArrayEnd();
	}

	TArray<FString> ToStringArray(const TSet<FName>& Names)
	{
		TArray<FString> Strings;
		Strings.Reserve(Names.Num());
		for (const FName& Name : Names)
		{
			Strings.Add(Name.ToString());
		}
		return Strings;
	}
}

FString ExportSemanticLabelTableToJson(const UDataTable* SemanticLabelTable)
{
	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json);

	JsonWriter->WriteArrayStart();

	if (SemanticLabelTable)
	{
		TArray<TPair<FString, const FSemanticLabel*>> Rows;
		SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""), [&Rows](const FName& Key, const FSemanticLabel& Value)
		{
			Rows.Emplace(Key.ToString(), &Value);
		});
		Rows.Sort([](const TPair<FString, const FSemanticLabel*>& A, const TPair<FString, const FSemanticLabel*>& B)
		{
			return A.Key < B.Key;
		});

		for (const auto& [RowName, Row] : Rows)
		{
			JsonWriter->WriteObjectStart();
			JsonWriter->WriteValue(TEXT("Name"), RowName);
			JsonWriter->WriteValue(TEXT("Label"), Row->Label);

			TArray<FString> ActorTypes;
			for (const TSubclassOf<AActor>& ActorType : Row->ActorTypes)
			{
				if (const UClass* ActorClass = ActorType.Get())
				{
					ActorTypes.Add(ActorClass->GetPathName());
				}
			}
			WriteSortedStringArray(JsonWriter, TEXT("ActorTypes"), ActorTypes);

			TArray<FString> ActorTags = ToStringArray(Row->ActorTags);
			WriteSortedStringArray(JsonWriter, TEXT("ActorTags"), ActorTags);

			TArray<FString> StaticMeshTypes;
			for (const TSoftObjectPtr<UStaticMesh>& StaticMeshType : Row->StaticMeshTypes)
			{
				if (!StaticMeshType.IsNull())
				{
					StaticMeshTypes.Add(StaticMeshType.ToSoftObjectPath().ToString());
				}
			}
			WriteSortedStringArray(JsonWriter, TEXT("StaticMeshTypes"), StaticMeshTypes);

			TArray<FString> SkeletalMeshTypes;
			for (const TSoftObjectPtr<USkeletalMesh>& SkeletalMeshType : Row->SkeletalMeshTypes)
			{
				if (!SkeletalMeshType.IsNull())
				{
					SkeletalMeshTypes.Add(SkeletalMeshType.ToSoftObjectPath().ToString());
				}
			}
			WriteSortedStringArray(JsonWriter, TEXT("SkeletalMeshTypes"), SkeletalMeshTypes);

			TArray<FString> ComponentTags = ToStringArray(Row->ComponentTags);
			WriteSortedStringArray(JsonWriter, TEXT("ComponentTags"), ComponentTags);

			JsonWriter->WriteObjectEnd();
		}
	}

	JsonWriter->WriteArrayEnd();
	JsonWriter->Close();

	return Json;
}
