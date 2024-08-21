// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGameMode.h"

const IActorClassificationInterface* ATempoGameMode::GetActorClassifier() const
{
	return Cast<IActorClassificationInterface>(GetWorld()->GetSubsystemBase(ActorClassifier));
}
