// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoScriptable.generated.h"

class UTempoScriptingServer;

UINTERFACE()
class UTempoEngineScriptable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Objects may implement ITempoEngineScriptable to register scripting services with the engine scripting server.
 */
class TEMPOSCRIPTING_API ITempoEngineScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterEngineServices(UTempoScriptingServer* ScriptingServer) = 0;
};

UINTERFACE()
class UTempoWorldScriptable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Objects may implement ITempoWorldScriptable to register scripting services with the world scripting server.
 */
class TEMPOSCRIPTING_API ITempoWorldScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) = 0;
};
