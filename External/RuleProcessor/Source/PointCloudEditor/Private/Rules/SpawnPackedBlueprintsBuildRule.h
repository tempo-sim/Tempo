// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "PointCloudSliceAndDiceShared.h"
#include "LevelInstance/LevelInstanceTypes.h"

#include "SpawnPackedBlueprintsBuildRule.generated.h"

class UPointCloud;
struct FSlateImageBrush;
class ISlateStyle;
  
UENUM(BlueprintType)
enum class EPointCloudLevelInstanceType : uint8
{
	LevelInstance							UMETA(DisplayName = "Level Instance"),
	PackedLevelInstance						UMETA(DisplayName = "Packed Level Instance"),
	PackedLevelInstanceBlueprint			UMETA(DisplayName = "Packed Level Instance Blueprint"),
};

USTRUCT(BlueprintType)
struct FSpawnPackedBlueprintsBuildRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FSpawnPackedBlueprintsBuildRuleData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	EPointCloudLevelInstanceType LevelInstanceType = EPointCloudLevelInstanceType::PackedLevelInstanceBlueprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	bool bExternalActors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ContentDir))
	FDirectoryPath ContentFolder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Pivot)
	ELevelInstancePivotType PivotType = ELevelInstancePivotType::CenterMinZ;
};

UCLASS(BlueprintType, hidecategories = (Object))
class USpawnPackedBlueprintsBuildRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	USpawnPackedBlueprintsBuildRule();

public:
	static const SIZE_T SUBLEVEL_SLOT = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FSpawnPackedBlueprintsBuildRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::GENERATOR; }
	virtual const FPointCloudRuleData* GetData() const override { return &Data; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool Compile(FSliceAndDiceContext& Context) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context) const override;
	//~ End UPointCloudRule Interface
};

class FSpawnPackedBlueprintsBuildRuleInstance : public FPointCloudRuleInstanceWithData<FSpawnPackedBlueprintsBuildRuleInstance, FSpawnPackedBlueprintsBuildRuleData>
{
public:
	FSpawnPackedBlueprintsBuildRuleInstance(const USpawnPackedBlueprintsBuildRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	// only override postexecute since we need to wait for our subrule to execute so we can use the actors it generates
	virtual bool PostExecuteInternal(FSliceAndDiceExecutionContextPtr Context) override;
};

class FSpawnPackedBlueprintsBuildFactory : public FSliceAndDiceRuleFactory
{
public:
	FSpawnPackedBlueprintsBuildFactory(TSharedPtr<ISlateStyle> Style);
	virtual ~FSpawnPackedBlueprintsBuildFactory();

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
