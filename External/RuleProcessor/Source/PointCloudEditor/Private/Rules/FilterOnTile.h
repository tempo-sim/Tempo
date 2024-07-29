// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "FilterOnTile.generated.h"

USTRUCT(BlueprintType)
struct FTileFilterRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:

	FTileFilterRuleData();

	void OverrideNameValue(bool bInsideSlot);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumTilesX = 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumTilesY = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumTilesZ = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 TileX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 TileY = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 TileZ = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	EPointCloudBoundsOption BoundsOption = EPointCloudBoundsOption::Compute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FBox Bounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString NamePattern;

	/** Performs basic data validation on the tile indices */
	bool Validate() const;
};

UCLASS(BlueprintType, hideCategories = (Object))
class UTileFilterRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UTileFilterRule();

public:
	static const SIZE_T INSIDE_TILE = 0;
	static const SIZE_T OUTSIDE_TILE = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FTileFilterRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::FILTER; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool Compile(FSliceAndDiceContext& Context) const override;	

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const  override;
	//~ End UPointCloudRule Interface
};

class FTileFilterRuleInstance : public FPointCloudRuleInstanceWithData<FTileFilterRuleInstance, FTileFilterRuleData>
{
public:
	FTileFilterRuleInstance(const UTileFilterRule* Rule, bool bInInvertSelection)
		: FPointCloudRuleInstanceWithData(Rule, Rule->Data)
		, bInvertSelection(bInInvertSelection)
	{
	}

	virtual bool Execute() override;

private:
	bool bInvertSelection;
};

class FTileFilterFactory : public FSliceAndDiceRuleFactory
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
