// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PointCloud.h"
 
/**
 * Implements the PointCloud module.
 */
class FPointCloudModule
	: public IModuleInterface
{
public:

	//~ IModuleInterface interface

	virtual void StartupModule() override 
	{ 
		
	}
	virtual void ShutdownModule() override 
	{ 		
	}

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}	
};

IMPLEMENT_MODULE(FPointCloudModule, PointCloud);
