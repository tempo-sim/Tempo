// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Logs
DECLARE_LOG_CATEGORY_EXTERN(LogMassTrafficEditor, Log, All);

// CVars
extern int32 GDebugMassTrafficEditor;

class FMassTrafficEditorModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
