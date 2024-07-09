// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "PointCloudSliceAndDiceRuleSet.h"
#include "Styling/SlateStyle.h"
#include "Brushes/SlateImageBrush.h"

#include "ExternalRule.generated.h"

UCLASS(BlueprintType, hideCategories = (Object))
class UExternalRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UExternalRule();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	TObjectPtr<UPointCloudSliceAndDiceRuleSet> RuleSet;

	/** Data is trivial here because we use it just to support overrides */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FPointCloudRuleData Data;

	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	
	/** Callback on pre-change so we can detach from external rule set events */
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	
	/** Attach to rule set updates post-loading */
	virtual void PostLoad() override;
	//~ End UObject Interface

	//~ Begin UPointCloudRule Interface
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::GENERATOR; }
	virtual bool Compile(FSliceAndDiceContext& Context) const override;

	/** Need to override the behavior to query the data in the external rule set */
	virtual TMap<FName, const FPointCloudRuleData*> GetOverrideableProperties() const override;

	/** Need to override the behavior to query the data in the external rule set */
	virtual void GetOverrideableProperties(TMap<FName, const FPointCloudRuleData*>& Properties) const override;

	/** Custom setter for rule parent so we can prevent recursion */
	virtual void SetParentRule(UPointCloudRule* InParentRule) override;
	
	/** Callback on post-change so we can update & attach to external rule set events */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface

private:
	/** Callback to update local information & propagate changes upwards */
	void OnRuleSetUpdated();

	/** Updates slots based on rule set */
	bool UpdateRuleSet();

	/** Mutable variables to prevent reentry */
	mutable bool bIsBeingCompiled = false;
	mutable bool bIsUpdating = false;
};

/** Dummy-type rule instance used to support overrides */
class FExternalRuleInstance : public FPointCloudRuleInstanceWithData<FExternalRuleInstance, FPointCloudRuleData>
{
public:
	FExternalRuleInstance(const UExternalRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}
};

class FExternalRuleFactory : public FSliceAndDiceRuleFactory
{
public:
	FExternalRuleFactory(TSharedPtr<ISlateStyle> Style);
	virtual ~FExternalRuleFactory();

	//~ Begin FSliceAndDiceRuleFactory Interface
public:
	virtual FString Name() const override;
	virtual FString Description() const override;
	virtual FSlateBrush* GetIcon() const override;
	virtual UPointCloudRule::RuleType GetType() const override { return UPointCloudRule::RuleType::GENERATOR; }

protected:
	virtual UPointCloudRule* Create(UObject* Parent) override;
	//~ End FSliceAndDiceRuleFactory Interface

private:
	FSlateImageBrush* Icon;
};
