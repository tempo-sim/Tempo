// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"

class UDataTable;
class UMaterialInstanceDynamic;

void TEMPOSENSORS_API OptimizeShowFlagsForNoColor(FEngineShowFlags& ShowFlags);

// Configure the given post-process settings, show flags, and ray-tracing toggle for a
// photorealistic-quality scene capture: Lumen GI + reflections, MegaLights, histogram auto exposure,
// the full set of color-relevant show flags (tonemapper, bloom, lens flares, vignette, DOF,
// ambient occlusion, volumetric fog, etc.), and ray tracing on. Applied to both UTempoCamera (always)
// and UTempoLidar (only when bColorEnabled is true).
void TEMPOSENSORS_API ApplyPhotorealisticRenderSettings(FPostProcessSettings& OutPostProcess,
	FEngineShowFlags& OutShowFlags, bool& OutUseRayTracingIfEnabled);

// Resolve the settings' overridable/overriding label row names against the active semantic label
// table and push the resulting label IDs onto a sensor's post-process material instance, which
// substitutes the overriding label wherever an object labeled overridable carries a non-zero
// subsurface color. Sets OverridingLabel to 0 — disabling the substitution — whenever the pair
// does not resolve, so clearing the row names at runtime takes effect on an already-built MID.
void TEMPOSENSORS_API ApplyLabelOverrideParameters(UMaterialInstanceDynamic* MaterialInstance);

// Serialize a semantic label table to an Unreal DataTable JSON document, in the format
// UDataTable::CreateTableFromJSONString reads back — so a client can fetch the live table, edit it,
// and load it again. UDataTable's own GetTableAsJSON is editor-only, hence this.
//
// Rows, and the entries within each row, are sorted by name: the table's row map and the row's
// TSet columns iterate in an order that says nothing, and an export meant to be diffed has to be
// stable across runs. Unresolved entries are dropped, having no path left to write.
FString TEMPOSENSORS_API ExportSemanticLabelTableToJson(const UDataTable* SemanticLabelTable);
