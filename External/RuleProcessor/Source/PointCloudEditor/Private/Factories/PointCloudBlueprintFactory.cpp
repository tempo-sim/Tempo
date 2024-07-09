// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudBlueprintFactory.h"
#include "Blueprints/PointCloudBlueprint.h"
#include "Rules/ExecuteBlueprintRule.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"


UPointCloudBlueprintFactory::UPointCloudBlueprintFactory(const FObjectInitializer& InObjectInitializer)
: UFactory(InObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = false;
	SupportedClass = UPointCloudBlueprint::StaticClass();
	ParentClass = UPointCloudBlueprintObject::StaticClass();
}

UPointCloudBlueprintFactory::~UPointCloudBlueprintFactory()
{
}

UObject* UPointCloudBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{

	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, BlueprintType, UPointCloudBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (NewBlueprint)
	{
		TArray<TObjectPtr<UEdGraph>>& UbergraphPages = NewBlueprint->UbergraphPages;
		if (UbergraphPages.Num() == 1)
		{
			TObjectPtr<UEdGraph> UberGraphPage = UbergraphPages[0];

			int32 NodePositionY = 0;
			FKismetEditorUtilities::AddDefaultEventNode(NewBlueprint, UberGraphPage, FName(TEXT("Execute")), UPointCloudBlueprintObject::StaticClass(), NodePositionY);
		}
	}

	return NewBlueprint;
}
