// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoServiceProvider.generated.h"

class FTempoServer;

UINTERFACE()
class TEMPOCORE_API UTempoServiceProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Objects may implement ITempoServiceProvider to register their gRPC services
 * with the Tempo API server.
 */
class TEMPOCORE_API ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) = 0;
};
