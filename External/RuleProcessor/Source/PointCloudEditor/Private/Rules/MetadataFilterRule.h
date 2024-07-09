// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleFactory.h"

#include "MetadataFilterRule.generated.h"

UENUM()
enum class EPointCloudMetadataFilterType
{	
	Value					UMETA(DisplayName = "Matches the given value exactly"),
	Pattern					UMETA(DisplayName = "Matches against Pattern"),	
};

USTRUCT(BlueprintType)
struct FMetadataFilterRuleData : public FPointCloudRuleData
{
	GENERATED_BODY()

	virtual UScriptStruct* GetStruct() const override { return StaticStruct(); }

public:

	FMetadataFilterRuleData();

	/** Writes in the name pattern to the final value */
	void OverrideNameValue(const FString& RowKey);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString NamePattern = TEXT("$IN_VALUE_$METADATAKEY_$ROWKEY_$METADATAVALUE");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Key;

	// Making this invisible as it is deprecated
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use Value And Row Key Map Instead"))
	FString Value;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FString, FString> ValueAndRowKeyMap; 

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EPointCloudMetadataFilterType FilterType = EPointCloudMetadataFilterType::Value;
};

UCLASS(BlueprintType, hideCategories = (Object))
class UMetadataFilterRule : public UPointCloudRule
{
	GENERATED_BODY()

protected:
	UMetadataFilterRule();

public:
	static const SIZE_T MATCHES_EXPRESSION = 0;
	static const SIZE_T DOESNT_MATCH_EXPRESSION = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FMetadataFilterRuleData Data;

	//~ Begin UObject Interface
	/**
	* On deserialize copy Old Values into the new value row map
	* @param Ar - A reference to the Archive to serialized to / from
	*/
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

	//~ Begin UPointCloudRule Interface
public:
	virtual FString Description() const override;
	virtual FString RuleName() const override;
	virtual RuleType GetType() const override { return RuleType::FILTER; }
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const override;
	virtual bool Compile(FSliceAndDiceContext& Context) const override;	

protected:
	virtual void ReportParameters(FSliceAndDiceContext& Context)  const override;
	//~ End UPointCloudRule Interface
};

class FMetadataFilterRuleInstance : public FPointCloudRuleInstanceWithData<FMetadataFilterRuleInstance, FMetadataFilterRuleData>
{
public:
	FMetadataFilterRuleInstance(const UMetadataFilterRule* InRule, bool bInMatchesFilter)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
		, bMatchesFilter(bInMatchesFilter)
	{
	}

	virtual bool Execute() override;

private:
	bool bMatchesFilter;
};

class FMetadataFilterRuleFactory : public FSliceAndDiceRuleFactory
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
