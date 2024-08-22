// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "MassTrafficLightRegistrySubsystem.h"


int32 UMassTrafficLightRegistrySubsystem::RegisterTrafficLightType(const FMassTrafficLightTypeData& InTrafficLightType)
{
	const int32 TrafficLightTypeIndex = TrafficLightTypes.Find(InTrafficLightType);
	
	if (TrafficLightTypeIndex != INDEX_NONE)
	{
		return TrafficLightTypeIndex;
	}

	return TrafficLightTypes.Add(InTrafficLightType);
}

void UMassTrafficLightRegistrySubsystem::RegisterTrafficLight(const FMassTrafficLightInstanceDesc& TrafficLightInstanceDesc)
{
	TrafficLightInstanceDescs.Add(TrafficLightInstanceDesc);
}

void UMassTrafficLightRegistrySubsystem::ClearTrafficLights()
{
	TrafficLightTypes.Empty();
	TrafficLightInstanceDescs.Empty();
}

const TArray<FMassTrafficLightTypeData>& UMassTrafficLightRegistrySubsystem::GetTrafficLightTypes() const
{
	return TrafficLightTypes;
}

const TArray<FMassTrafficLightInstanceDesc>& UMassTrafficLightRegistrySubsystem::GetTrafficLightInstanceDescs() const
{
	return TrafficLightInstanceDescs;
}

bool UMassTrafficLightRegistrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
