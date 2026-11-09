// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "MassTrafficControllerRegistrySubsystem.h"

#include "MassTraffic.h"


int32 UMassTrafficControllerRegistrySubsystem::RegisterTrafficLightType(const FMassTrafficLightTypeData& InTrafficLightType)
{
	const int32 TrafficLightTypeIndex = TrafficLightTypes.Find(InTrafficLightType);
	
	if (TrafficLightTypeIndex != INDEX_NONE)
	{
		return TrafficLightTypeIndex;
	}

	return TrafficLightTypes.Add(InTrafficLightType);
}

void UMassTrafficControllerRegistrySubsystem::RegisterTrafficLight(const FMassTrafficLightInstanceDesc& TrafficLightInstanceDesc)
{
	TrafficLightInstanceDescs.Add(TrafficLightInstanceDesc);
}

void UMassTrafficControllerRegistrySubsystem::RegisterTrafficSign(const FMassTrafficSignInstanceDesc& TrafficSignInstanceDesc)
{
	TrafficSignInstanceDescs.Add(TrafficSignInstanceDesc);
}

void UMassTrafficControllerRegistrySubsystem::RegisterTrafficLightsFromDataAsset(const UMassTrafficLightInstancesDataAsset& InstancesDataAsset, const UMassTrafficLightTypesDataAsset* TypesDataAsset)
{
	bool bIsAlreadyRegistered = false;
	RegisteredTrafficLightDataAssets.Add(FObjectKey(&InstancesDataAsset), &bIsAlreadyRegistered);
	if (bIsAlreadyRegistered)
	{
		return;
	}

	// The baked type indices refer to TypesDataAsset; the registry may already hold other types, and dedupes equal ones.
	TArray<int16> RegistryTypeIndices;
	if (TypesDataAsset != nullptr)
	{
		RegistryTypeIndices.Reserve(TypesDataAsset->TrafficLightTypes.Num());
		for (const FMassTrafficLightTypeData& TrafficLightType : TypesDataAsset->TrafficLightTypes)
		{
			RegistryTypeIndices.Add(static_cast<int16>(RegisterTrafficLightType(TrafficLightType)));
		}
	}

	TrafficLightInstanceDescs.Reserve(TrafficLightInstanceDescs.Num() + InstancesDataAsset.TrafficLights.Num());
	for (FMassTrafficLightInstanceDesc TrafficLightInstanceDesc : InstancesDataAsset.TrafficLights)
	{
		// An unknown type is left for the intersection spawn data generator to pick a compatible one.
		TrafficLightInstanceDesc.TrafficLightTypeIndex = RegistryTypeIndices.IsValidIndex(TrafficLightInstanceDesc.TrafficLightTypeIndex)
			? RegistryTypeIndices[TrafficLightInstanceDesc.TrafficLightTypeIndex]
			: static_cast<int16>(INDEX_NONE);
		RegisterTrafficLight(TrafficLightInstanceDesc);
	}

	UE_LOG(LogMassTraffic, Log, TEXT("Registered %d baked traffic lights from %s."), InstancesDataAsset.TrafficLights.Num(), *InstancesDataAsset.GetName());
}

void UMassTrafficControllerRegistrySubsystem::ClearTrafficControllers()
{
	TrafficLightTypes.Empty();
	TrafficLightInstanceDescs.Empty();
	TrafficSignInstanceDescs.Empty();
	RegisteredTrafficLightDataAssets.Empty();
}

const TArray<FMassTrafficLightTypeData>& UMassTrafficControllerRegistrySubsystem::GetTrafficLightTypes() const
{
	return TrafficLightTypes;
}

const TArray<FMassTrafficLightInstanceDesc>& UMassTrafficControllerRegistrySubsystem::GetTrafficLightInstanceDescs() const
{
	return TrafficLightInstanceDescs;
}

const TArray<FMassTrafficSignInstanceDesc>& UMassTrafficControllerRegistrySubsystem::GetTrafficSignInstanceDescs() const
{
	return TrafficSignInstanceDescs;
}

bool UMassTrafficControllerRegistrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
