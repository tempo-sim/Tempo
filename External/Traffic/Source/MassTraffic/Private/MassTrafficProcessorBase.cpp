// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficProcessorBase.h"
#include "MassCommonUtils.h"


void UMassTrafficProcessorBase::InitializeInternal(UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::InitializeInternal(InOwner, EntityManager);

	// Get settings
	MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	LogOwner = UWorld::GetSubsystem<UMassTrafficSubsystem>(InOwner.GetWorld());

	// Seed random stream
	const int32 TrafficRandomSeed = UE::Mass::Utils::OverrideRandomSeedForTesting(MassTrafficSettings->RandomSeed);
	if (TrafficRandomSeed > 0 || UE::Mass::Utils::IsDeterministic())
	{
		RandomStream.Initialize(TrafficRandomSeed);
	}
	else
	{
		RandomStream.GenerateNewSeed();
	}
}
