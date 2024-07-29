// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudFactoryNew.h"

#include "PointCloudImpl.h"


/* UPointCloudFactoryNew structors
 *****************************************************************************/

UPointCloudFactoryNew::UPointCloudFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UPointCloudImpl::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}


/* UFactory overrides
 *****************************************************************************/

UObject* UPointCloudFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(InClass && InClass->IsChildOf(UPointCloud::StaticClass()) && InClass != UPointCloud::StaticClass());
	return NewObject<UPointCloud>(InParent, InClass, InName, Flags);
}


bool UPointCloudFactoryNew::ShouldShowInNewMenu() const
{
	return true;
}
