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
class TEMPOSCRIPTING_API UTempoScriptingEngineSubsystem : public UEngineSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UTempoScriptingEngineSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual bool IsTickable() const override { return bIsInitialized; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	UPROPERTY()
	UTempoScriptingServer* ScriptingServer;

	UPROPERTY()
	bool bIsInitialized = false;
};
