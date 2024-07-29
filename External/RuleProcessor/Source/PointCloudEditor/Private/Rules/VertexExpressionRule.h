// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "VertexExpressionRule.generated.h"

USTRUCT(BlueprintType)
struct FVertexExpressionRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString Expression = "Minz > 10 AND Minz < 200";
};

UCLASS(BlueprintType, hideCategories = (Object))
class UVertexExpressionRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UVertexExpressionRule();

public:
	static const SIZE_T MATCHES_EXPRESSION = 0;
	static const SIZE_T DOESNT_MATCH_EXPRESSION = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FVertexExpressionRuleData Data;

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

class FVertexExpressionRuleInstance : public FPointCloudRuleInstanceWithData<FVertexExpressionRuleInstance, FVertexExpressionRuleData>
{
public:
	FVertexExpressionRuleInstance(const UVertexExpressionRule* InRule, bool bInMatchesExpression)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
		, bMatchesExpression(bInMatchesExpression)
	{
	}

	virtual bool Execute() override;

private:
	bool bMatchesExpression;
};

class FVertexExpressionRuleFactory : public FSliceAndDiceRuleFactory
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
