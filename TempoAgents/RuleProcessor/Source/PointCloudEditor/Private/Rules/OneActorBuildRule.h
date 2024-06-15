// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "PointCloudAssetHelpers.h"

#include "OneActorBuildRule.generated.h"

class UPointCloud;
class UStaticMesh;
struct FSlateImageBrush;
class ISlateStyle;
class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;

USTRUCT(BlueprintType)
struct FOneActorBuildRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FOneActorBuildRuleData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString NamePattern;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FName FolderPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString PerModuleAttributeKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	bool bSingleInstanceAsStaticMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	bool bUseHierarchicalInstancedStaticMeshComponent = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Pivot)
	EPointCloudPivotType PivotType = EPointCloudPivotType::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UStaticMesh>> ComponentOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<AActor> TemplateActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<UInstancedStaticMeshComponent> TemplateISM = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TemplateHISM = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Templates)
	TObjectPtr<UStaticMeshComponent> TemplateStaticMeshComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GroupId)
	bool bManualGroupId = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GroupId)
	int32 GroupId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	FSpawnAndInitMaterialOverrideParameters MaterialOverrides;
};
  
UCLASS(BlueprintType, hidecategories = (Object))
class UOneActorBuildRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UOneActorBuildRule();

public:
	/**
	* Make The Name String for the Given Point Cloud By Substituting tokens in the RuleName Template
	* @return A String containing the name of the new actor to create, or empty string on failure
	* @param Pc - A pointer to the point cloud
	* @param InNamePattern - the pattern name to replace
	* @param InNameValue - token to replace for $IN_VALUE
	*/
	static FString MakeName(UPointCloud* Pc, const FString& InNamePattern, const FString& InNameValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FOneActorBuildRuleData Data;

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

class FOneActorRuleInstance : public FPointCloudRuleInstanceWithData<FOneActorRuleInstance, FOneActorBuildRuleData>
{
public:
	explicit FOneActorRuleInstance(const UOneActorBuildRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual FString GetHash() override;
	virtual bool Execute(FSliceAndDiceExecutionContextPtr Context) override;

	virtual bool CanBeExecutedOnAnyThread() const override { return false; }
};

class FOneActorBuildFactory : public FSliceAndDiceRuleFactory
{
public:
	FOneActorBuildFactory(TSharedPtr<ISlateStyle> Style);
	virtual ~FOneActorBuildFactory();

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
 