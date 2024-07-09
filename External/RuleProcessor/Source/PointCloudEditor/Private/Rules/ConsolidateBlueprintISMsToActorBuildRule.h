// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"

#include "ConsolidateBlueprintISMsToActorBuildRule.generated.h"

class UPointCloud;
struct FSlateImageBrush;
class ISlateStyle;

USTRUCT(BlueprintType)
struct FConsolidateBlueprintISMsToActorBuildRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FConsolidateBlueprintISMsToActorBuildRuleData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	TMap< TSubclassOf<class AActor>, TSubclassOf<class AActor> > OverrideActorsMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString NamePattern;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString BlueprintMetadataKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString ActorMetadataKey;
		
	/** DataLayers the generated actors will belong to.*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = DataLayers)
	TArray<FActorDataLayer> DataLayers;
};
  
UCLASS(BlueprintType, hidecategories = (Object))
class UConsolidateBlueprintISMsToActorBuildRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UConsolidateBlueprintISMsToActorBuildRule();

public:
	/**
	* Make The Name String for the Given Point Cloud By Substituting tokens in the RuleName Template
	* @return A String containing the name of the new actor to create, or empty string on failure
	* @param Pc - A pointer to the point cloud
	* @param MetadataValue - the metadata value associated
	* @param InNamePattern - the naming pattern
	* @param InNameValue - the token to replace $IN_VALUE with
	* @param InIndex - the token to replace $INDEX with
	*/
	static FString MakeName(UPointCloud* Pc, const FString& MetadataValue, const FString& InNamePattern, const FString& InNameValue, int32 InIndex);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FConsolidateBlueprintISMsToActorBuildRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::GENERATOR; }
	virtual bool Compile(FSliceAndDiceContext& Context) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FConsolidateBlueprintISMsToActorBuildRuleInstance : public FPointCloudRuleInstanceWithData<FConsolidateBlueprintISMsToActorBuildRuleInstance, FConsolidateBlueprintISMsToActorBuildRuleData>
{
public:
	explicit FConsolidateBlueprintISMsToActorBuildRuleInstance(const UConsolidateBlueprintISMsToActorBuildRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual FString GetHash() override;
	virtual bool Execute(FSliceAndDiceExecutionContextPtr Context) override;

	virtual bool CanBeExecutedOnAnyThread() const override { return false; }
};

class FConsolidateBlueprintISMsToActorBuildFactory : public FSliceAndDiceRuleFactory
{
public:
	FConsolidateBlueprintISMsToActorBuildFactory(TSharedPtr<ISlateStyle> Style);
	virtual ~FConsolidateBlueprintISMsToActorBuildFactory();

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
