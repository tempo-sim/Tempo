// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "Engine/Blueprint.h"
#include "Blueprints/PointCloudBlueprint.h"

#include "ExecuteBlueprintRule.generated.h"

USTRUCT(BlueprintType)
struct FExecuteBlueprintRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FExecuteBlueprintRuleData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UPointCloudBlueprint> ExecuteBlueprint = nullptr;
};

UCLASS(BlueprintType, hideCategories = (Object))
class UExecuteBlueprintRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UExecuteBlueprintRule();

public:
	static const SIZE_T BLUEPRINT_SLOT = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FExecuteBlueprintRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::GENERATOR; }
	virtual const FPointCloudRuleData* GetData() const override { return &Data; }
	virtual bool ShouldAlwaysReRun() const override { return true; }
	virtual bool Compile(FSliceAndDiceContext& Context) const override;
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context) const override;
	//~ End UPointCloudRule Interface
};

class FExecuteBlueprintRuleInstance : public FPointCloudRuleInstanceWithData<FExecuteBlueprintRuleInstance, FExecuteBlueprintRuleData>
{
private:
	typedef FPointCloudRuleInstanceWithData<FExecuteBlueprintRuleInstance, FExecuteBlueprintRuleData> Super;

public:
	FExecuteBlueprintRuleInstance(const UExecuteBlueprintRule* Rule, UClass* InPointCloudBlueprintObject)
		: FPointCloudRuleInstanceWithData(Rule, Rule->Data)
		, PointCloudBlueprintObject(InPointCloudBlueprintObject)
	{
	}

	virtual bool Execute(FSliceAndDiceExecutionContextPtr Context) override;
	virtual bool PostExecute(FSliceAndDiceExecutionContextPtr Context) override;

	virtual bool CanBeExecutedOnAnyThread() const { return false; }

private:
	FDelegateHandle OnActorSpawnedDelegateHandle;
	TArray<AActor*> SpawnedActors;

	UClass* PointCloudBlueprintObject;
};

class FExecuteBlueprintFactory : public FSliceAndDiceRuleFactory
{
	//~ Begin FSliceAndDiceRuleFactory Interface
public:
	virtual FString Name() const override;
	virtual FString Description() const override;
	virtual UPointCloudRule::RuleType GetType() const override { return UPointCloudRule::RuleType::GENERATOR; }

protected:
	virtual UPointCloudRule* Create(UObject* Parent) override;
	//~ End FSliceAndDiceRuleFactory Interface
};
