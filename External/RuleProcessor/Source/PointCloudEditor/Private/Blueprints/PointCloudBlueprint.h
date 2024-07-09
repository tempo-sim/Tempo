// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/Blueprint.h"
#include "EditorUtilityBlueprint.h"

#include "PointCloudBlueprint.generated.h"

UCLASS(NotBlueprintType, notplaceable)
class UPointCloudBlueprint : public UEditorUtilityBlueprint
{
	GENERATED_BODY()
public:
	UPointCloudBlueprint(const FObjectInitializer& InObjectInitializer);

	virtual bool SupportedByDefaultBlueprintFactory() const override
	{
		return false;
	}
};

UCLASS(Blueprintable, notplaceable)
class UPointCloudBlueprintObject : public UObject
{
	GENERATED_BODY()
public:
	UPointCloudBlueprintObject(const FObjectInitializer& InObjectInitializer);

	UFUNCTION(BlueprintImplementableEvent)
	void Execute(class UPointCloudView* View);

	void SetWorld(UWorld* InWorld) { World = InWorld; }
	virtual UWorld* GetWorld() const override { return World; }

private:
	UWorld* World = nullptr;
};