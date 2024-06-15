// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRule.h"
#include "PointCloudSliceAndDiceRuleInstance.h"
#include "PointCloudSliceAndDiceRuleData.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "SequenceRule.generated.h"

USTRUCT(BlueprintType)
struct FSequenceRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	
	UPROPERTY()
	int NumSlots = 5;	
};

UCLASS(BlueprintType, hideCategories = (Object))
class USequenceRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	USequenceRule();

public:
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FSequenceRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::FILTER; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool Compile(FSliceAndDiceContext& Context) const override;	
	
protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FSequenceRuleInstance : public FPointCloudRuleInstanceWithData<FSequenceRuleInstance, FSequenceRuleData>
{
public:
	FSequenceRuleInstance(const USequenceRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual bool Execute() override;
};

class FSequenceRuleFactory : public FSliceAndDiceRuleFactory
{
	//~ Begin FSliceAndDiceRuleFactory Interface
public:
	virtual FString Name() const override;
	virtual FString Description() const override;
	virtual UPointCloudRule::RuleType GetType() const override { return UPointCloudRule::RuleType::FILTER; }

protected:
	virtual UPointCloudRule* Create(UObject* Parent) override;
	//~ End FSliceAndDiceRuleFactory Interface
};
