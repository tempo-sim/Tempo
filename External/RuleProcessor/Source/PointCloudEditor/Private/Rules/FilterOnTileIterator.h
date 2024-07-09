// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "FilterOnTileIterator.generated.h"

USTRUCT(BlueprintType)
struct FFilterOnTileIteratorData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:

	FFilterOnTileIteratorData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumTilesX = 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumTilesY = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumTilesZ = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	EPointCloudBoundsOption	 BoundsOption = EPointCloudBoundsOption::Compute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FBox Bounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString NamePattern;

	/** Using token substitution build a name string for the current file, this replaces keywords in the NamePattern 
	* The following keywords are replaced 
	*
	* $X -> x
	* $Y -> y
	* $Z -> z
	* $XDIM ->NumTilesX 
	* $YDIM ->NumTilesY
	* $ZDIM ->NumTilesZ
	*
	*/
	FString BuildNameString(int32 x, int32 y, int32 z) const;

	/** Updates the previous name value */
	void OverrideNameValue(int32 InTileX, int32 InTileY, int32 InTileZ);
};

UCLASS(BlueprintType, hideCategories = (Object))
class UFilterOnTileIterator : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UFilterOnTileIterator();

public:
	static const SIZE_T INSIDE_TILE = 0;	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FFilterOnTileIteratorData Data;

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

class FTileIteratorFilterInstance : public FPointCloudRuleInstanceWithData<FTileIteratorFilterInstance, FFilterOnTileIteratorData>
{
public:
	FTileIteratorFilterInstance(const UFilterOnTileIterator* InRule, int32 InX, int32 InY, int32 InZ)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
		, TileX(InX)
		, TileY(InY)
		, TileZ(InZ)
	{
	}

	virtual bool Execute() override;
	virtual bool PostExecute() override;

private:
	int32 TileX;
	int32 TileY;
	int32 TileZ;
};

class FTileIteratorFilterFactory : public FSliceAndDiceRuleFactory
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
