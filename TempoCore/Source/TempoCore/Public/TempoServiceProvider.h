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
 *
 * Handlers always run on the game thread, but not always inside a frame. While a client
 * drives a FixedStep step one substep at a time (UTempoCoreSettings::SubstepsPerStep), its
 * RPCs are handled between the substeps, between frames. There, world time and delta
 * seconds are the latest substep's, but nothing else has moved: actors and components have
 * not ticked, timers have not fired, and GFrameCounter is the last frame's. An RPC that
 * needs a step boundary and a world tick (it changes the step grid, loads a level, or
 * changes engine state) declares it at registration with AtStepBoundary().
 */
class TEMPOCORE_API ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) = 0;
};
