// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "MassTrafficControllerRegistrySubsystem.h"


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

void UMassTrafficControllerRegistrySubsystem::ClearTrafficControllers()
{
	TrafficLightTypes.Empty();
	TrafficLightInstanceDescs.Empty();
	TrafficSignInstanceDescs.Empty();
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
