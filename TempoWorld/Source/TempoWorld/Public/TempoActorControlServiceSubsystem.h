// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoActorControlServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoWorld
{
	class SpawnActorRequest;
	class SpawnActorResponse;
	class FinishSpawningActorRequest;
	class DestroyActorRequest;
	class SetActorTransformRequest;
	class SetComponentTransformRequest;
	class GetAllActorsRequest;
	class GetAllActorsResponse;
	class GetAllComponentsRequest;
	class GetAllComponentsResponse;
	class GetActorPropertiesRequest;
	class GetComponentPropertiesRequest;
	class GetPropertiesResponse;
	class ActivateComponentRequest;
	class DeactivateComponentRequest;
}

UCLASS()
class TEMPOWORLD_API UTempoActorControlServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void SpawnActor(const TempoWorld::SpawnActorRequest& Request, const TResponseDelegate<TempoWorld::SpawnActorResponse>& ResponseContinuation);
	
	void FinishSpawningActor(const TempoWorld::FinishSpawningActorRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation);

	void DestroyActor(const TempoWorld::DestroyActorRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void SetActorTransform(const TempoWorld::SetActorTransformRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void SetComponentTransform(const TempoWorld::SetComponentTransformRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void ActivateComponent(const TempoWorld::ActivateComponentRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void DeactivateComponent(const TempoWorld::DeactivateComponentRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;
	
	void GetAllActors(const TempoWorld::GetAllActorsRequest& Request, const TResponseDelegate<TempoWorld::GetAllActorsResponse>& ResponseContinuation) const;

	void GetAllComponents(const TempoWorld::GetAllComponentsRequest& Request, const TResponseDelegate<TempoWorld::GetAllComponentsResponse>& ResponseContinuation) const;
	
	void GetActorProperties(const TempoWorld::GetActorPropertiesRequest& Request, const TResponseDelegate<TempoWorld::GetPropertiesResponse>& ResponseContinuation) const;

	void GetComponentProperties(const TempoWorld::GetComponentPropertiesRequest& Request, const TResponseDelegate<TempoWorld::GetPropertiesResponse>& ResponseContinuation) const;

protected:
	template <typename RequestType>
	void SetProperty(const RequestType& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	UPROPERTY()
	TMap<const AActor*, FTransform> DeferredSpawnTransforms;
};
