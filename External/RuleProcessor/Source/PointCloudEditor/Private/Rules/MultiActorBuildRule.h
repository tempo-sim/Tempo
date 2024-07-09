// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloudSliceAndDiceRuleFactory.h"
#include "PointCloud.h"
#include "PointCloudAssetHelpers.h"

#include "MultiActorBuildRule.generated.h"

class ISlateStyle;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UPointCloud;
struct FSlateImageBrush;

USTRUCT(BlueprintType)
struct FMultiActorBuildRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FMultiActorBuildRuleData();

	/** Writes in the name pattern to the final value */
	void OverrideNameValue();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString NamePattern;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString MetadataKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSingleInstanceAsStaticMesh = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUseHierarchicalInstancedStaticMeshComponent = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UStaticMesh>> ComponentOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<AActor> TemplateActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<UInstancedStaticMeshComponent> TemplateISM = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TemplateHISM = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<UStaticMeshComponent> TemplateStaticMeshComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Pivot, DisplayName = "Fallback Pivot Type")
	EPointCloudPivotType PivotType = EPointCloudPivotType::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Pivot)
	FString PivotKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Pivot)
	FString PivotValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString PerModuleAttributeKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GroupId)
	bool bManualGroupId = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FName FolderPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GroupId)
	int32 GroupId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	FSpawnAndInitMaterialOverrideParameters MaterialOverrides;
};
 
UCLASS(BlueprintType, hidecategories = (Object))
class UMultiActorBuildRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UMultiActorBuildRule();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FMultiActorBuildRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::GENERATOR; }
	virtual bool Compile(FSliceAndDiceContext &Context) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FMultiActorRuleInstance : public FPointCloudRuleInstanceWithData<FMultiActorRuleInstance, FMultiActorBuildRuleData>
{
public:
	FMultiActorRuleInstance(const UMultiActorBuildRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual FString GetHash() override;
	virtual bool Execute(FSliceAndDiceExecutionContextPtr Context) override;

	virtual bool CanBeExecutedOnAnyThread() const override { return false; }
};

class FMultiActorBuildFactory : public FSliceAndDiceRuleFactory
{
public:
	FMultiActorBuildFactory(TSharedPtr<ISlateStyle> Style);
	virtual ~FMultiActorBuildFactory();

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
 