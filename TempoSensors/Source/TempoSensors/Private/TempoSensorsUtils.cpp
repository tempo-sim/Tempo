// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsUtils.h"

#include "TempoLabelTypes.h"
#include "TempoSensorsConstants.h"
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

	ShowFlags.SetLensDistortion(false);
	ShowFlags.SetMegaLights(false);
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

TArray<FString> ValidateSemanticLabelTable(const UDataTable* SemanticLabelTable)
{
	TArray<FString> Problems;

	if (!SemanticLabelTable)
	{
		Problems.Add(TEXT("No semantic label table is set."));
		return Problems;
	}

	// Which row already claimed each key, so the second claimant can name the first. A DataTable's
	// row names are unique, so only the columns can collide.
	TMap<const UClass*, FName> ClaimedActorTypes;
	TMap<FString, FName> ClaimedMeshPaths;
	TMap<FName, FName> ClaimedActorTags;
	TMap<FName, FName> ClaimedComponentTags;

	// A row claiming a key a previous row already claimed. BuildLabelMaps resolves the collision by
	// keeping the first, which means the second row's entry silently does nothing.
	auto ReportCollision = [&Problems](const TCHAR* Kind, const FString& Key, const FName& FirstRow, const FName& SecondRow)
	{
		Problems.Add(FString::Printf(TEXT("%s '%s' is claimed by rows '%s' and '%s'; only '%s' will take effect."),
			Kind, *Key, *FirstRow.ToString(), *SecondRow.ToString(), *FirstRow.ToString()));
	};

	SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""),
		[&](const FName& RowName, const FSemanticLabel& Row)
	{
		if (Row.Label < 0 || Row.Label > GTempoCamera_Max_Label)
		{
			// The camera packs the label into the exponent field of the tile atlas's fp32 alpha,
			// biased by +1. Outside this range that field saturates to 0 or 255, making the alpha
			// subnormal or Inf/NaN, and the GPU destroys it — the pixel decodes to label 0 at
			// MaxDepth, so the depth goes with the label.
			Problems.Add(FString::Printf(TEXT("Row '%s' has label ID %d, outside the encodable range 0-%d. Label and depth would both be corrupted wherever this label is visible."),
				*RowName.ToString(), Row.Label, GTempoCamera_Max_Label));
		}

		for (const TSubclassOf<AActor>& ActorType : Row.ActorTypes)
		{
			const UClass* ActorClass = ActorType.Get();
			if (!ActorClass)
			{
				// The importer resolves a class path eagerly and only logs when it cannot, leaving
				// a null behind. The path it failed on is gone by the time we see the row, so all
				// we can report is that the row carries one.
				Problems.Add(FString::Printf(TEXT("Row '%s' has an actor type that did not resolve to a class. Check the paths in its ActorTypes for a typo."),
					*RowName.ToString()));
				continue;
			}
			if (const FName* FirstRow = ClaimedActorTypes.Find(ActorClass))
			{
				ReportCollision(TEXT("Actor type"), ActorClass->GetPathName(), *FirstRow, RowName);
				continue;
			}
			ClaimedActorTypes.Add(ActorClass, RowName);
		}

		// Static and skeletal meshes are labeled from separate columns but share one asset path
		// space, so they are checked for collisions against each other as well as themselves.
		auto ValidateMeshColumn = [&](const TCHAR* Kind, const FSoftObjectPath& AssetPath, const UObject* LoadedAsset)
		{
			if (AssetPath.IsNull())
			{
				Problems.Add(FString::Printf(TEXT("Row '%s' has an empty %s entry."), *RowName.ToString(), Kind));
				return;
			}
			if (!LoadedAsset)
			{
				Problems.Add(FString::Printf(TEXT("Row '%s' names %s '%s', which did not load as that type."),
					*RowName.ToString(), Kind, *AssetPath.ToString()));
				return;
			}
			const FString MeshFullPath = LoadedAsset->GetPathName();
			if (const FName* FirstRow = ClaimedMeshPaths.Find(MeshFullPath))
			{
				ReportCollision(TEXT("Mesh"), MeshFullPath, *FirstRow, RowName);
				return;
			}
			ClaimedMeshPaths.Add(MeshFullPath, RowName);
		};

		for (const TSoftObjectPtr<UStaticMesh>& StaticMeshAsset : Row.StaticMeshTypes)
		{
			ValidateMeshColumn(TEXT("static mesh"), StaticMeshAsset.ToSoftObjectPath(), StaticMeshAsset.LoadSynchronous());
		}

		for (const TSoftObjectPtr<USkeletalMesh>& SkeletalMeshAsset : Row.SkeletalMeshTypes)
		{
			ValidateMeshColumn(TEXT("skeletal mesh"), SkeletalMeshAsset.ToSoftObjectPath(), SkeletalMeshAsset.LoadSynchronous());
		}

		for (const FName& ActorTag : Row.ActorTags)
		{
			if (ActorTag.IsNone())
			{
				Problems.Add(FString::Printf(TEXT("Row '%s' has an empty actor tag."), *RowName.ToString()));
				continue;
			}
			if (const FName* FirstRow = ClaimedActorTags.Find(ActorTag))
			{
				ReportCollision(TEXT("Actor tag"), ActorTag.ToString(), *FirstRow, RowName);
				continue;
			}
			ClaimedActorTags.Add(ActorTag, RowName);
		}

		for (const FName& ComponentTag : Row.ComponentTags)
		{
			if (ComponentTag.IsNone())
			{
				Problems.Add(FString::Printf(TEXT("Row '%s' has an empty component tag."), *RowName.ToString()));
				continue;
			}
			if (const FName* FirstRow = ClaimedComponentTags.Find(ComponentTag))
			{
				ReportCollision(TEXT("Component tag"), ComponentTag.ToString(), *FirstRow, RowName);
				continue;
			}
			ClaimedComponentTags.Add(ComponentTag, RowName);
		}
	});

	if (const FSemanticLabel* NoLabelRow = SemanticLabelTable->FindRow<FSemanticLabel>(FName(GNoLabelRowName), TEXT(""), false))
	{
		if (NoLabelRow->Label != 0)
		{
			// An object matching no row keeps FInstanceSemanticIdPair's default semantic ID of 0
			// and is drawn with stencil 0, whatever this row says. A non-zero value here would name
			// a class that nothing is ever labeled with.
			Problems.Add(FString::Printf(TEXT("Row '%s' has label ID %d, but unmatched objects are always labeled 0. It must be 0."),
				GNoLabelRowName, NoLabelRow->Label));
		}
	}
	else
	{
		Problems.Add(FString::Printf(TEXT("Table has no '%s' row, which names the label worn by everything the table does not match."),
			GNoLabelRowName));
	}

	return Problems;
}

bool ResolveLabelRowOverrides(const UDataTable* SemanticLabelTable, int32& OutOverridableLabel, int32& OutOverridingLabel)
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	const FName OverridableLabelRowName = TempoSensorsSettings->GetOverridableLabelRowName();
	const FName OverridingLabelRowName = TempoSensorsSettings->GetOverridingLabelRowName();

	if (!SemanticLabelTable || OverridableLabelRowName.IsNone() || OverridingLabelRowName.IsNone())
	{
		return false;
	}

	const FSemanticLabel* OverridableRow = SemanticLabelTable->FindRow<FSemanticLabel>(OverridableLabelRowName, TEXT(""), false);
	const FSemanticLabel* OverridingRow = SemanticLabelTable->FindRow<FSemanticLabel>(OverridingLabelRowName, TEXT(""), false);
	if (!OverridableRow || !OverridingRow)
	{
		return false;
	}

	OutOverridableLabel = OverridableRow->Label;
	OutOverridingLabel = OverridingRow->Label;
	return true;
}

void ApplyLabelOverrideParameters(UMaterialInstanceDynamic* MaterialInstance)
{
	if (!MaterialInstance)
	{
		return;
	}

	int32 OverridableLabel = 0;
	int32 OverridingLabel = 0;
	if (ResolveLabelRowOverrides(GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable(), OverridableLabel, OverridingLabel))
	{
		MaterialInstance->SetScalarParameterValue(TEXT("OverridableLabel"), OverridableLabel);
		MaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), OverridingLabel);
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
