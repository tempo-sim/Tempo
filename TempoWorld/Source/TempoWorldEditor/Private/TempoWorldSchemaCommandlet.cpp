// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSchemaCommandlet.h"

#include "TempoWorldSchema.h"

#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogTempoWorldSchemaCommandlet, Log, All);

UTempoWorldSchemaCommandlet::UTempoWorldSchemaCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UTempoWorldSchemaCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	TempoWorldSchema::FOptions Options;

	if (const FString* Out = ParamVals.Find(TEXT("Out")))
	{
		Options.OutputPath = *Out;
	}
	else
	{
		Options.OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TempoSchema"), TEXT("tempo_schema.json"));
	}
	Options.OutputPath = FPaths::ConvertRelativePathToFull(Options.OutputPath);

	if (const FString* BaseClasses = ParamVals.Find(TEXT("BaseClasses")))
	{
		BaseClasses->ParseIntoArray(Options.BaseClasses, TEXT(","), /*InCullEmpty*/ true);
		for (FString& BaseClass : Options.BaseClasses)
		{
			BaseClass.TrimStartAndEndInline();
		}
	}

	Options.bIncludeBlueprintClasses = !Switches.Contains(TEXT("NoBlueprints"));
	Options.bBlueprintAccessibleOnly = !Switches.Contains(TEXT("AllProperties"));

	UE_LOG(LogTempoWorldSchemaCommandlet, Display, TEXT("Writing schema to %s"), *Options.OutputPath);

	TempoWorldSchema::FStats Stats;
	FString Error;
	if (!TempoWorldSchema::Write(Options, Stats, Error))
	{
		UE_LOG(LogTempoWorldSchemaCommandlet, Error, TEXT("Failed to write schema: %s"), *Error);
		return 1;
	}

	UE_LOG(LogTempoWorldSchemaCommandlet, Display, TEXT("Wrote schema: %s"), *Stats.ToString());
	return 0;
}
