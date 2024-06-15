// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Engine/Blueprint.h"
#include "Factories/Factory.h"

#include "PointCloudBlueprintFactory.generated.h"

UCLASS(HideCategories = Object, MinimalAPI)
class UPointCloudBlueprintFactory : public UFactory
{
	GENERATED_BODY()
public:
	UPointCloudBlueprintFactory(const FObjectInitializer& InObjectInitializer);
	~UPointCloudBlueprintFactory();

	// The type of blueprint that will be created
	UPROPERTY(EditAnywhere, Category = PointCloudBlueprintFactory)
	TEnumAsByte<enum EBlueprintType> BlueprintType;

	// The parent class of the created blueprint
	UPROPERTY(EditAnywhere, Category = PointCloudBlueprintFactory)
	TSubclassOf<UObject> ParentClass;

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool CanCreateNew() const override { return true; }
};
