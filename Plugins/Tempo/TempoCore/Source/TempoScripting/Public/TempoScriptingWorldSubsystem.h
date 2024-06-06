// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoScriptingWorldSubsystem.generated.h"

/**
 * Holds the World scripting server.
 */
UCLASS()
class TEMPOSCRIPTING_API UTempoScriptingWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UTempoScriptingWorldSubsystem();

	/* UWorldSubsystem Interface */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	// Initialize is too early to register services (objects exist but haven't been added to the world yet),
	// so use OnWorldBeginPlay instead.
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	UPROPERTY()
	UTempoScriptingServer* ScriptingServer;
};
