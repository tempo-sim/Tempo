// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloudSliceAndDiceRuleFactory.h"
#include "PointCloud.h"

#include "MetadataIteratorRule.generated.h"

class UPointCloud;
struct FSlateImageBrush;
class ISlateStyle;
  
USTRUCT(BlueprintType)
struct FMetadataIteratorRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FMetadataIteratorRuleData();

	/** Writes in the name pattern to the final value */
	void OverrideNameValue();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString NamePattern;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString MetadataKey;

	// this is used by the hidden rule for filtering but we don't it to be visible to the user
	UPROPERTY();
	FString MetadataValue;
};

UCLASS(BlueprintType, hidecategories = (Object))
class UMetadataIteratorRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UMetadataIteratorRule();

public:
	static const SIZE_T SUBLEVEL_SLOT = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FMetadataIteratorRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::ITERATOR; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool Compile(FSliceAndDiceContext& Context) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context) const override;
	//~ End UPointCloudRule Interface
};

class FMetadataIteratorRuleInstance : public FPointCloudRuleInstanceWithData<FMetadataIteratorRuleInstance, FMetadataIteratorRuleData>
{
public:
	FMetadataIteratorRuleInstance(const UMetadataIteratorRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual bool Execute() override;
	virtual bool PostExecute() override;
};

class FMetadataIteratorRuleFactory : public FSliceAndDiceRuleFactory
{
	//~ Begin FSliceAndDiceRuleFactory Interface
public:
	virtual FString Name() const override;
	virtual FString Description() const override;
	virtual UPointCloudRule::RuleType GetType() const override { return UPointCloudRule::RuleType::FILTER; } // TODO change this to generator when the slice'n'dice ui displays generators

protected:
	virtual UPointCloudRule* Create(UObject* Parent) override;
	//~ End FSliceAndDiceRuleFactory Interface
};
 