// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "GameFramework/Actor.h"

#include "DebugBuildRule.generated.h"

class UPointCloud;
struct FSlateImageBrush;
class ISlateStyle;

UENUM(BlueprintType)
enum class EDebugBuildRuleMesh : uint8
{
	DebugBuildRuleMesh_Sphere	UMETA(DisplayName = "Sphere"),
	DebugBuildRuleMesh_Cube		UMETA(DisplayName = "Cube"),
	DebugBuildRuleMesh_Axis		UMETA(DisplayName = "Axis")
};

USTRUCT(BlueprintType)
struct FDebugBuildRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FDebugBuildRuleData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString NamePattern;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	EDebugBuildRuleMesh DebugMesh = EDebugBuildRuleMesh::DebugBuildRuleMesh_Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	float ScaleFactor = 1.0f;
};
  
UCLASS(BlueprintType, hidecategories = (Object))
class UDebugBuildRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UDebugBuildRule();

public:
	/**
	* Make The Name String for the Given Point Cloud By Substituting tokens in the RuleName Template
	* @return A String containing the name of the new actor to create, or empty string on failure
	* @param Pc - A pointer to the point cloud
	* @param MetadataValue - the metadata value associated
	* @param InNamePattern - the naming pattern
	* @param InNameValue - the token to replace $IN_VALUE with
	*/
	static FString MakeName(UPointCloud* Pc, const FString& MetadataValue, const FString& InNamePattern, const FString& InNameValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FDebugBuildRuleData Data;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override {	return RuleType::GENERATOR; }
	virtual bool Compile(FSliceAndDiceContext& Context) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FDebugBuildRuleInstance : public FPointCloudRuleInstanceWithData<FDebugBuildRuleInstance, FDebugBuildRuleData>
{
public:
	explicit FDebugBuildRuleInstance(const UDebugBuildRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual FString GetHash() override;
	virtual bool Execute() override;

	virtual bool CanBeExecutedOnAnyThread() const override { return false; }
};

class FDebugBuildFactory : public FSliceAndDiceRuleFactory
{
public:

	FDebugBuildFactory(TSharedPtr<ISlateStyle> Style);
	virtual ~FDebugBuildFactory();

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
