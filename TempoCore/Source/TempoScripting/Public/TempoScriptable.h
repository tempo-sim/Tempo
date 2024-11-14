// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoScriptable.generated.h"

class FTempoScriptingServer;

UINTERFACE()
class TEMPOSCRIPTING_API UTempoScriptable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Objects may implement ITempoScriptable to register scripting services with the scripting server.
 */
class TEMPOSCRIPTING_API ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) = 0;
};
