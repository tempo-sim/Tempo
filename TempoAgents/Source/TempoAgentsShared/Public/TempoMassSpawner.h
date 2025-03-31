// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "TempoSubsystems.h"

#include "CoreMinimal.h"
#include "MassSpawner.h"
#include "TempoMassSpawner.generated.h"

/*
 * These two classes (UTempoMassSpawnerSubSystem and ATempoMassSpawner) exist to work around two issues in
 * MassAgentSubsystem and MassSpawner.
 *
 * The first issue is that MassAgentSubsystem only handles pending MassAgent component registrations on Tick, meaning
 * any spawns that happen on BeginPlay will not take into account fragments created by MassAgent components (for
 * example, obstacle locations).
 *
 * The second issue is that MassSpawner checks IsLoaded on all its EntityConfigs and, if they are not loaded, calls
 * FStreamableManager::RequestAsyncLoad. That will lead to at least a one-frame delay any time EntityConfigs were not
 * loaded before BeginPlay (which seems to always be the case; whatever they're doing in
 * AMassSpawner::PostRegisterAllComponents does not seem to be working).
 *
 * It just so happens that, in practice, these two issues "cancel out": the agents are not initialized until the first
 * Tick, but the spawn doesn't actually happen until the first Tick anyhow.
 *
 * However, during deferred level loading (see ATempoGameMode::StartPlay), we call
 * UEngine::BlockTillLevelStreamingCompleted, which causes the EntityConfigs to load and the spawn to happen on
 * BeginPlay, exposing the consequences of the first issue above.
 *
 * We could have intentionally delayed the spawn until the first tick, but we would rather the spawn happen during
 * BeginPlay, so we added a workaround for each of the above issues here.
 */

UCLASS()
class TEMPOAGENTSSHARED_API UTempoMassSpawnerSubSystem : public UTempoGameWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
};

UCLASS()
class TEMPOAGENTSSHARED_API ATempoMassSpawner : public AMassSpawner
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
};
