// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudFactory.h"

#include "Containers/UnrealString.h"
#include "PointCloudImpl.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


/* UPointCloudFactory structors
 *****************************************************************************/

UPointCloudFactory::UPointCloudFactory( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
{
	Formats.Add(FString(TEXT("psv;")) + NSLOCTEXT("UPointCloudFactory", "FormatCsv", "CSV File").ToString());
	Formats.Add(FString(TEXT("psz;")) + NSLOCTEXT("UPointCloudFactory", "FormatPsz", "Zipped PSV File").ToString());
	Formats.Add(FString(TEXT("pbc;")) + NSLOCTEXT("UPointCloudFactory", "FormatPbc", "Alembic File").ToString());
	
	SupportedClass = UPointCloudImpl::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
}

UObject* UPointCloudFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(InClass && InClass->IsChildOf(UPointCloud::StaticClass()) && InClass != UPointCloud::StaticClass());
	return NewObject<UPointCloud>(InParent, InClass, InName, Flags);
}

UObject* UPointCloudFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	check(InClass && InClass->IsChildOf(UPointCloud::StaticClass()) && InClass != UPointCloud::StaticClass());
	UPointCloud* PointCloud = NewObject<UPointCloud>(InParent, InClass, InName, Flags);

	if (Warn)
	{
		Warn->BeginSlowTask(FText::FromString("Loading Point Cloud From File"),true,false);
	}
	
	FString Extension = FPaths::GetExtension(Filename).ToLower();
	if (Extension == FString("psv"))
	{		
		if (!PointCloud->LoadFromCsv(Filename, FBox(EForceInit::ForceInit), UPointCloud::REPLACE, Warn))
		{
			bOutOperationCanceled = true;
		}
		else
		{
			bOutOperationCanceled = false;
		}
	}
	else if (Extension == FString("pbc"))
	{
		if (!PointCloud->LoadFromAlembic(Filename, FBox(EForceInit::ForceInit), UPointCloud::REPLACE, Warn))
		{
			bOutOperationCanceled = true;
		}
		else
		{
			bOutOperationCanceled = false;
		}
	}
	
	if (Warn)
	{
		Warn->EndSlowTask();
	}

	return PointCloud;
}
