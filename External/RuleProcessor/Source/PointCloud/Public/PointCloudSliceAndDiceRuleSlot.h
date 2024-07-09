// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"

#include "PointCloudSliceAndDiceRuleSlot.generated.h"

class UPointCloudRule;

/** A Slot is a place a rule can be placed. Rules may have none, one or more slots. This object stores information about a slot 
*/
UCLASS(BlueprintType)
class POINTCLOUD_API UPointCloudRuleSlot : public UObject
{
	GENERATED_BODY()

public:
	UPointCloudRuleSlot();

	/** Returns label of this slot, either the custom one or the defaulted one if locally empty */
	FString GetLabel() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	bool bExternallyVisible = true;

	UPROPERTY()
	FGuid Guid;

#if WITH_EDITOR
	/** Propagates changes from slot to rule when property changes */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Bind this slot to a slot in an external rule set */
	bool SetTwinSlot(UPointCloudRuleSlot* InTwinSlot);

	/** Sets the rule this slot is in */
	void SetRule(UPointCloudRule* InRule, SIZE_T SlotIndex);

protected:
	UPointCloudRule* Rule = nullptr;
	SIZE_T SlotIndex = 0;

	UPointCloudRuleSlot* TwinSlot = nullptr;
#endif
};