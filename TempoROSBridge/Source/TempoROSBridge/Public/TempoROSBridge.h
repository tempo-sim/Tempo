// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TempoROSNode.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FTempoROSBridgeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// TUniquePtr<FTempoROSNode> TempoROSNode;
};
