// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "PerPointIterator.generated.h"

USTRUCT(BlueprintType)
struct FPerPointIteratorData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:

	FPerPointIteratorData();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString NamePattern;
	
	FString BuildNameString(int32 VertexId) const;

	/** Updates the previous name value */
	void OverrideNameValue(int32 VertexId);
};

UCLASS(BlueprintType, hideCategories = (Object))
class UPerPointIterator : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UPerPointIterator();

public:
	static const SIZE_T PER_POINT = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FPerPointIteratorData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::ITERATOR; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool Compile(FSliceAndDiceContext& Context) const override;	

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FPerPointIteratorFilterInstance : public FPointCloudRuleInstanceWithData<FPerPointIteratorFilterInstance, FPerPointIteratorData>
{
public:
	FPerPointIteratorFilterInstance(const UPerPointIterator* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual bool PreExecute(FSliceAndDiceExecutionContextPtr Context) override;
	bool Iterate(FSliceAndDiceExecutionContextPtr Context);
	virtual bool PostExecute() override;

	/** Returns true if this can be executed on any thread, false otherwise */
	virtual bool CanBeExecutedOnAnyThread() const override { return false; }
};

class FPerPointIteratorFilterFactory : public FSliceAndDiceRuleFactory
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
