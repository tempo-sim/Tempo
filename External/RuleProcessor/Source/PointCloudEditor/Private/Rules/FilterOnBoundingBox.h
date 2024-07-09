// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "FilterOnBoundingBox.generated.h"

class UPointCloud;

USTRUCT(BlueprintType)
struct FBoundingBoxFilterRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FBoundingBoxFilterRuleData();

	void OverrideNameValue(bool bInsideSlot);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FBox Bounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString NamePattern;
};
 
UCLASS(BlueprintType, hidecategories = (Object))
class UBoundingBoxFilterRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UBoundingBoxFilterRule();

public:
	static const SIZE_T INSIDE_SLOT = 0;
	static const SIZE_T OUTSIDE_SLOT = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FBoundingBoxFilterRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::FILTER; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool Compile(FSliceAndDiceContext &Context) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FBoundingBoxRuleInstance : public FPointCloudRuleInstanceWithData<FBoundingBoxRuleInstance, FBoundingBoxFilterRuleData>
{
public:
	FBoundingBoxRuleInstance(const UBoundingBoxFilterRule* InRule, bool bInInvertSelection)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
		, bInvertSelection(bInInvertSelection)
	{
	}

	virtual bool Execute() override;

private:
	bool bInvertSelection;
};

class FBoundingBoxFilterFactory : public FSliceAndDiceRuleFactory
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
 