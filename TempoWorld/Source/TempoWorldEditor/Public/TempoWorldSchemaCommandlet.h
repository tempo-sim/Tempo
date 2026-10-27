// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Commandlets/Commandlet.h"

#include "TempoWorldSchemaCommandlet.generated.h"

/**
 * Writes this build's tempo_schema.json, the input the generated client stubs are produced from.
 *
 *   <Editor-Cmd> <Project> -run=TempoWorldSchema [-Out=<path>] [-BaseClasses=A,B]
 *                          [-NoBlueprints] [-AllProperties]
 *
 * Defaults to Saved/TempoSchema/tempo_schema.json, walks every UClass including Blueprint generated
 * classes, and emits only what Blueprint can reach -- which is what the typed API exposes.
 * -AllProperties lifts that filter, for seeing how much of the reflected surface is Blueprint-hidden.
 */
UCLASS()
class TEMPOWORLDEDITOR_API UTempoWorldSchemaCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UTempoWorldSchemaCommandlet();

	virtual int32 Main(const FString& Params) override;
};
