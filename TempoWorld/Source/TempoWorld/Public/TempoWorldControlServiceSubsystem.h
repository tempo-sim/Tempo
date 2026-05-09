// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoWorldControlServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
}

namespace TempoWorld
{
	class SpawnActorRequest;
	class SpawnActorResponse;
	class AddComponentRequest;
	class AddComponentResponse;
	class DestroyComponentRequest;
	class FinishSpawningActorRequest;
	class FinishSpawningActorResponse;
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
	class CallFunctionRequest;
}

DECLARE_MULTICAST_DELEGATE(FTempoWorldControlServiceActivated);
DECLARE_MULTICAST_DELEGATE(FTempoWorldControlServiceDeactivated);

UCLASS()
class TEMPOWORLD_API UTempoWorldControlServiceSubsystem : public UTempoWorldSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	void SpawnActor(const TempoWorld::SpawnActorRequest& Request, const TResponseDelegate<TempoWorld::SpawnActorResponse>& ResponseContinuation);
	
	void FinishSpawningActor(const TempoWorld::FinishSpawningActorRequest& Request, const TResponseDelegate<TempoWorld::FinishSpawningActorResponse>& ResponseContinuation);

	void DestroyActor(const TempoWorld::DestroyActorRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void AddComponent(const TempoWorld::AddComponentRequest& Request, const TResponseDelegate<TempoWorld::AddComponentResponse>& ResponseContinuation) const;

	void DestroyComponent(const TempoWorld::DestroyComponentRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void SetActorTransform(const TempoWorld::SetActorTransformRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void SetComponentTransform(const TempoWorld::SetComponentTransformRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void ActivateComponent(const TempoWorld::ActivateComponentRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void DeactivateComponent(const TempoWorld::DeactivateComponentRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;
	
	void GetAllActors(const TempoWorld::GetAllActorsRequest& Request, const TResponseDelegate<TempoWorld::GetAllActorsResponse>& ResponseContinuation) const;

	void GetAllComponents(const TempoWorld::GetAllComponentsRequest& Request, const TResponseDelegate<TempoWorld::GetAllComponentsResponse>& ResponseContinuation) const;
	
	void GetActorProperties(const TempoWorld::GetActorPropertiesRequest& Request, const TResponseDelegate<TempoWorld::GetPropertiesResponse>& ResponseContinuation) const;

	void GetComponentProperties(const TempoWorld::GetComponentPropertiesRequest& Request, const TResponseDelegate<TempoWorld::GetPropertiesResponse>& ResponseContinuation) const;

	void CallObjectFunction(const TempoWorld::CallFunctionRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void OnTempoWorldControlServiceActivated();

	void OnTempoWorldControlServiceDeactivated();
	
protected:
	UPROPERTY()
	TMap<const AActor*, FTransform> DeferredSpawnTransforms;

private:
	template <typename RequestType>
	void SetProperty(const RequestType& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	static FTempoWorldControlServiceActivated TempoWorldControlServiceActivated;
	static FTempoWorldControlServiceDeactivated TempoWorldControlServiceDeactivated;
};
