// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

/**
 * Writes tempo_schema.json: the language-neutral description of this build's reflected types that
 * the generated client stubs are produced from.
 *
 * This runs in the editor, not against a packaged sim, for two reasons. Metadata is editor-only
 * (WITH_METADATA is WITH_EDITORONLY_DATA), so a cooked build cannot report a property's tooltip,
 * its category, or -- the one that matters -- its BlueprintGetter/BlueprintSetter, without which the
 * typed API would write through a property whose setter is the only thing that clamps it or marks
 * render state dirty. And an editor build is something CI already produces, so the schema can be
 * diffed on every pull request rather than first appearing at release time.
 *
 * The packaged build is still the ground truth for what ships: a separate integration test launches
 * it and asserts every class, property and function in here resolves at runtime.
 *
 * Output is deterministic -- classes, properties, functions, structs and enums are all emitted in
 * sorted order, with no timestamps -- so two releases diff cleanly and a breaking change is a
 * reviewable list rather than an archaeology exercise.
 */
namespace TempoWorldSchema
{
	struct FOptions
	{
		// Where to write. Required.
		FString OutputPath;

		// Restrict the walk to subclasses of these (by name or path). Empty walks every UClass.
		TArray<FString> BaseClasses;

		// Load every Blueprint generated class from the asset registry. This is the expensive part
		// of the walk, and the part whose cost scales with the project rather than the engine.
		bool bIncludeBlueprintClasses = true;

		// Emit only what Blueprint can reach, which is what the typed API exposes. The string-keyed
		// RPCs stay unfiltered regardless; this is about the stubs.
		bool bBlueprintAccessibleOnly = true;
	};

	struct FStats
	{
		int32 Classes = 0;
		int32 BlueprintClasses = 0;
		int32 Properties = 0;
		int32 PropertiesFiltered = 0;
		int32 PropertiesUnsupported = 0;
		int32 WritableProperties = 0;
		int32 Functions = 0;
		int32 FunctionsFiltered = 0;
		int32 UncallableFunctions = 0;
		int32 Structs = 0;
		int32 Enums = 0;
		double BlueprintLoadSeconds = 0.0;
		double WalkSeconds = 0.0;
		double WriteSeconds = 0.0;
		int64 OutputBytes = 0;
		FString ContentHash;

		FString ToString() const;
	};

	// Walks the reflected types and writes the schema. Returns false and fills OutError on failure.
	TEMPOWORLDEDITOR_API bool Write(const FOptions& Options, FStats& OutStats, FString& OutError);
}
