// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"

#include "TempoScriptingEngineSubsystem.generated.h"

/**
 * Holds the Engine scripting server.
 */
UCLASS()
class TEMPOSCRIPTING_API UTempoScriptingEngineSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	UTempoScriptingEngineSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	UPROPERTY()
	UTempoScriptingServer* ScriptingServer;
};
