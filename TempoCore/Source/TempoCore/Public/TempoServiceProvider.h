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
 * Handlers always run on the game thread, but not always inside a frame. With more than one
 * FixedStep step per game frame (UTempoCoreSettings::StepsPerGameFrame), a client that
 * advances one step at a time has its RPCs handled between those steps, between frames.
 * There, world time and delta seconds are the latest step's, but nothing else has moved:
 * actors and components have not ticked, timers have not fired, and GFrameCounter is the
 * last frame's. An RPC that needs a game frame and a world tick (it changes the step grid,
 * loads a level, or changes engine state) declares it at registration with GameFrameOnly().
 *
 * A streaming handler is invoked once per response: for the request, then again each time its
 * last response has been written. It must register its interest and respond later, from a tick
 * or an event, never from the invocation itself. The server finishes in-flight writes inside
 * the world tick (FTempoServer::FinishWrites) and, in FixedStep, in its own tick, waiting for
 * them as long as they take; a handler that responded at once would run there, and never let
 * them drain.
 */
class TEMPOCORE_API ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) = 0;
};
