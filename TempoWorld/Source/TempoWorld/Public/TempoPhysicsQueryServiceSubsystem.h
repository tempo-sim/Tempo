// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoPhysicsQueryServiceSubsystem.generated.h"

namespace TempoWorld
{
	class RaycastRequest;
	class RaycastResponse;
}

UCLASS()
class TEMPOWORLD_API UTempoPhysicsQueryServiceSubsystem : public UTempoWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	void Raycast(
		const TempoWorld::RaycastRequest& Request,
		const TResponseDelegate<TempoWorld::RaycastResponse>& ResponseContinuation) const;
};
