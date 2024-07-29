// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "ExportFBXRule.generated.h"

class UPointCloud;
struct FSlateImageBrush;
class ISlateStyle;

USTRUCT(BlueprintType)
struct FExportFBXRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:
	FExportFBXRuleData();

	void OverrideNameValue();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString NamePattern;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FDirectoryPath ExportDirectory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bOverwriteExistingFile = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAutomated = true;
};

UCLASS(BlueprintType, hidecategories = (Object))
class UExportFBXRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UExportFBXRule();

public:
	/**
	* Make The Name String for the Given Point Cloud By Substituting tokens in the RuleName Template
	* @return A String containing the name of the new actor to create, or empty string on failure
	* @param Pc - A pointer to the point cloud
	*/
	static FString MakeName(UPointCloud* Pc, const FString& InNamePattern);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FExportFBXRuleData Data;

	mutable TOptional<bool> OverwriteAllFiles;

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::GENERATOR; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool Compile(FSliceAndDiceContext& Context) const override;

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FExportFBXRuleInstance : public FPointCloudRuleInstanceWithData<FExportFBXRuleInstance, FExportFBXRuleData>
{
public:
	FExportFBXRuleInstance(const UExportFBXRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	virtual bool Execute() override;
	virtual bool PostExecute() override;
};

class FExportFBXFactory : public FSliceAndDiceRuleFactory
{
public:
	FExportFBXFactory(TSharedPtr<ISlateStyle> Style);
	virtual ~FExportFBXFactory();

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
