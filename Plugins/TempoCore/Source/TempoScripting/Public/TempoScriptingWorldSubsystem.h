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

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UTempoScriptingServer* GetScriptingServer() const { return ScriptingServer; }

private:
	UFUNCTION()
	void InitServer() const;
	
	UPROPERTY()
	UTempoScriptingServer* ScriptingServer;
};
