// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRule.h"
#include "PointCloudSliceAndDiceRuleInstance.h"
#include "PointCloudSliceAndDiceRuleData.h"
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "PointCloudAssetHelpers.h"
#include "GameFramework/Actor.h"

#include "SpawnNiagaraRule.generated.h"

class UPointCloud;
struct FSlateImageBrush;
class ISlateStyle;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class ENiagaraSpawnMode : uint8
{
	Random UMETA(DisplayName = "Randomized Selection"),
	Data UMETA(DisplayName = "Data Driven Selection"),
};

USTRUCT(BlueprintType)
struct FSpawnNiagaraRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FSpawnNiagaraRuleData();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString NamePattern;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Niagara)
	TArray<TObjectPtr<UNiagaraSystem>> NiagaraSystems;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<AActor> TemplateActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FName FolderPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	ENiagaraSpawnMode SpawnMode = ENiagaraSpawnMode::Random;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString MetadataKey = PointCloudAssetHelpers::GetUnrealAssetMetadataKey();
	
	/** DataLayers the generated actors will belong to.*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = DataLayers)
	TArray<FActorDataLayer> DataLayers;
};
  
UCLASS(BlueprintType, hidecategories = (Object))
class USpawnNiagaraRule	: public UPointCloudRule
{
	GENERATED_BODY()

protected:
	USpawnNiagaraRule();

public:
	/**
	* Make The Name String for the Given Point Cloud By Substituting tokens in the RuleName Template
	* @return A String containing the name of the new actor to create, or empty string on failure
	* @param Pc - A pointer to the point cloud	
	* @param InNamePattern - the naming pattern
	* @param InNameValue - the token to replace $IN_VALUE with
	*/
	static FString MakeName(UPointCloud* Pc,const FString& InNamePattern, const FString& InNameValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FSpawnNiagaraRuleData Data;

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

class FSpawnNiagaraRuleInstance : public FPointCloudRuleInstanceWithData<FSpawnNiagaraRuleInstance, FSpawnNiagaraRuleData>
{
public:
	explicit FSpawnNiagaraRuleInstance(const USpawnNiagaraRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual FString GetHash() override;
	virtual bool Execute(FSliceAndDiceExecutionContextPtr Context) override;

	virtual bool CanBeExecutedOnAnyThread() const override { return false; }

	/** Return the string that Prefixes Niagara System Asset Paths
	* @return The prefix for Niagara System Asset Paths
	*/
	static const FString GetNiagaraSystemIdentifier();
};

class FSpawnNiagaraFactory : public FSliceAndDiceRuleFactory
{
public:
	FSpawnNiagaraFactory(TSharedPtr<ISlateStyle> Style);
	virtual ~FSpawnNiagaraFactory();

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
