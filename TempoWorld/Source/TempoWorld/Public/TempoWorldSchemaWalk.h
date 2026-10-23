// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoWorldReflection.h"

/**
 * Walks this build's reflected classes and reports what they declare.
 *
 * Lives in the runtime module, not beside the commandlet that writes tempo_schema.json, because two
 * things need it and only one of them is an editor: the commandlet generates the client stubs from
 * it, and DescribeClasses answers a running sim's callers with it -- including the test that checks
 * every generated class still resolves against the packaged build. Sharing the walk is what makes
 * that test meaningful; two walks could agree today and drift tomorrow.
 *
 * Every type decision comes from TempoWorldReflection, so what this reports and what SetProperty
 * accepts cannot disagree.
 */
namespace TempoWorldSchemaWalk
{
	struct FOptions
	{
		// Restrict the walk to subclasses of these (by name or path). Empty walks every UClass.
		TArray<FString> BaseClasses;

		// Load every Blueprint generated class from the asset registry first. TObjectIterator only
		// sees loaded classes, so a Blueprint nothing has touched is invisible without this -- and
		// it is the expensive part of a walk, the part that scales with the project rather than the
		// engine.
		bool bIncludeBlueprintClasses = true;

		// Report only what Blueprint can reach, which is what the typed API exposes. The
		// string-keyed RPCs stay unfiltered regardless; this is about the generated surface.
		bool bBlueprintAccessibleOnly = true;

		// Classify functions as well as properties. A caller that only wants the property surface
		// can skip roughly half the work.
		bool bIncludeFunctions = true;
	};

	/** A named property, paired with what the classifier made of it. */
	struct FMember
	{
		FString Name;
		TempoWorldReflection::FTypeInfo Type;
	};

	/**
	 * A component a class brings with it.
	 *
	 * A Blueprint declares these in its SimpleConstructionScript; a native class creates them as
	 * default subobjects in its constructor. Either way they exist on every instance under a known
	 * name, which is what lets a generated class offer `truck.Mesh` instead of making a caller
	 * remember the string.
	 */
	struct FComponentTemplate
	{
		// The name the component has on an instance, which is how every TempoWorld RPC addresses it.
		FString Name;
		// The component class's path.
		FString ClassPath;
	};

	struct FClassEntry
	{
		FString Name;
		FString ClassPath;
		FString SuperPath;
		FString Package;
		bool bAbstract = false;
		bool bBlueprint = false;
		// Declared on this class only. Inheritance is reconstructed from SuperPath, which is what
		// keeps AActor's members out of all two thousand of its subclasses.
		TArray<FMember> Properties;
		TArray<TempoWorldReflection::FFunctionInfo> Functions;
		// Declared on this class only, as the members are: an inherited component belongs to the
		// class that introduced it.
		TArray<FComponentTemplate> Components;
	};

	struct FStructEntry
	{
		FString Name;
		TArray<FMember> Members;
	};

	struct FEnumEntry
	{
		FString Name;
		TArray<FString> Enumerators;
	};

	struct FStats
	{
		int32 Classes = 0;
		int32 BlueprintClasses = 0;
		// Skipped as compilation by-products: skeletons, placeholders, reinstanced classes.
		int32 ArtifactClasses = 0;
		int32 Properties = 0;
		int32 PropertiesFiltered = 0;
		int32 PropertiesUnsupported = 0;
		int32 WritableProperties = 0;
		int32 Functions = 0;
		int32 FunctionsFiltered = 0;
		int32 UncallableFunctions = 0;
		double BlueprintLoadSeconds = 0.0;
		double WalkSeconds = 0.0;
	};

	/**
	 * Everything one walk found.
	 *
	 * Ordering is deterministic -- classes by path, members by name, structs and enums by name,
	 * parameters left in declaration order because that order is the calling convention. That is
	 * what lets two releases diff cleanly, so it is a property of the walk rather than of whoever
	 * serializes it.
	 */
	struct FResult
	{
		TArray<FClassEntry> Classes;
		TArray<FStructEntry> Structs;
		TArray<FEnumEntry> Enums;
		FStats Stats;
	};

	// The layout version of the schema these types describe. Bumped when the shape changes, so a
	// generator reading an older document says so rather than quietly producing half a client.
	// Reported by both the schema file and DescribeClasses.
	TEMPOWORLD_API FString GetSchemaVersion();

	// Walks the reflected classes. Returns false and fills OutError only when the options name a
	// base class that does not exist; an empty world is a valid, empty result.
	TEMPOWORLD_API bool Walk(const FOptions& Options, FResult& OutResult, FString& OutError);

	// Whether a class is a by-product of compilation -- a Blueprint skeleton, a linker placeholder,
	// a reinstanced or trashed class -- rather than part of the API. Editor-only, mostly, which is
	// why an editor-side walk has to exclude them: a packaged build does not have them.
	TEMPOWORLD_API bool IsCompilationArtifactClass(const UClass* Class);

	// A content hash identifying what a client could break against: names, types, writability and
	// signatures. Deliberately excludes documentation, so adding a tooltip is not a drift event.
	//
	// Shared so that the hash in a generated schema file and the hash a running sim reports are the
	// same number, which is what lets a client refuse stubs built against a different build.
	TEMPOWORLD_API FString HashResult(const FResult& Result);

	// The same question about one entry: can a client that was generated against this class, struct
	// or enum still address it.
	//
	// Two things need this rather than the whole-walk number. A published class package describes a
	// subset of a build by design (TYPED_WORLD_API_PLAN.md section 16), so no sim will ever report
	// a hash it could match, and comparing per entry is the only check available to it. And for a
	// full package it is a better check anyway: one number over the whole walk says only "something
	// moved", where today adding a single Blueprint invalidates every class in the build.
	//
	// HashResult is defined in terms of these, so there is one definition of what an entry's
	// identity is rather than two that could drift.
	TEMPOWORLD_API FString HashClass(const FClassEntry& Entry);
	TEMPOWORLD_API FString HashStruct(const FStructEntry& Entry);
	TEMPOWORLD_API FString HashEnum(const FEnumEntry& Entry);
}
