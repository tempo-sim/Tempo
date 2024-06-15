// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "FilterOnOrientedBoundingBoxIterator.generated.h"

class UPointCloud;

USTRUCT(BlueprintType)
struct FFilterOnOrientedBoundingBoxIteratorData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FFilterOnOrientedBoundingBoxIteratorData();

	/** Using token substitution build a name string for the current file, this replaces keywords in the NamePattern
	* The following keywords are replaced
	*
	* $I -> Index
	*/
	void OverrideNameValue(int32 BoxIndex);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString NamePattern;
};
 
UCLASS(BlueprintType, hidecategories = (Object))
class UFilterOnOrientedBoundingBoxIterator : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UFilterOnOrientedBoundingBoxIterator();

public:
	static const SIZE_T INSIDE_SLOT = 0;
	static const SIZE_T OUTSIDE_SLOT = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString OBBNameRegex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FFilterOnOrientedBoundingBoxIteratorData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::ITERATOR; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool ShouldAlwaysReRun() const override { return true; }
	virtual bool Compile(FSliceAndDiceContext &Context) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FFilterOnOrientedBoundingBoxIteratorInstance : public FPointCloudRuleInstanceWithData<FFilterOnOrientedBoundingBoxIteratorInstance, FFilterOnOrientedBoundingBoxIteratorData>
{
public:
	FFilterOnOrientedBoundingBoxIteratorInstance(const UFilterOnOrientedBoundingBoxIterator* InRule, const TArray<FTransform>& InBoxTransforms, int32 InBoxIndex, bool bInInvertSelection)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
		, BoxTransforms(InBoxTransforms)
		, BoxIndex(InBoxIndex)
		, bInvertSelection(bInInvertSelection)
	{
	}

	bool Execute() override;

private:
	TArray<FTransform> BoxTransforms;
	int32 BoxIndex;
	bool bInvertSelection;
};

class FOrientedBoundingBoxIteratorFilterFactory : public FSliceAndDiceRuleFactory
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
 