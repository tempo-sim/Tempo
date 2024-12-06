// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "PCGSettings.h"
#include "Elements/PCGPointProcessingElementBase.h"

#include "PCGCopyAttributeFromNearest.generated.h"

/**
 * Finds the nearest point (inherently a n*n operation) and copies the specified attribute from it.
 * This is very similar to and borrows a lot from PCGDistance.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGCopyAttributeFromNearestSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("CopyAttributeFromNearest")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCopyAttributeFromNearestSettings", "NodeTitle", "CopyAttributeFromNearest"); }
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	/** The attribute to copy from the input. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGAttributePropertySelector InputAttribute = FPCGAttributePropertySelector::CreateAttributeSelector(TEXT("$Density"));

	/** The attribute to store the copy in the output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGAttributePropertySelector OutputAttribute = FPCGAttributePropertySelector::CreateAttributeSelector(TEXT("$Density"));
	
	/** A maximum distance to search, which is used as an optimization */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "1", PCG_Overridable))
	double MaximumDistance = 20000.0;
};

class FPCGCopyAttributeFromNearestElement : public FPCGPointProcessingElementBase
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
