// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "PCGContext.h"
#include "PCGSettings.h"
#include "Async/PCGAsyncLoadingContext.h"

#include "Engine/DataTable.h"

#include "PCGAssignRandomMeshAttribute.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct FStaticMeshProportion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Proportion = 1.0;

	bool operator==(const FStaticMeshProportion& Other) const
	{
		return StaticMesh == Other.StaticMesh;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FStaticMeshProportion& Struct)
	{
		return GetTypeHash(Struct.StaticMesh);
	}
};

USTRUCT(BlueprintInternalUseOnly)
struct FStaticMeshDistribution: public FTableRowBase
{
	GENERATED_BODY()

	// The static meshes and associated relative probabilities to use.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<FStaticMeshProportion> StaticMeshDistribution;
};

/**
 * Assigns a random mesh from the specified data table row to the specified attribute name.
 * This is very similar to and borrows a lot from PCGDataTableElement. Also PCGCreateAttribute.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), meta=(Keywords = "random mesh attribute"))
class UPCGAssignRandomMeshAttributeSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	//~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("AssignRandomMeshAttribute")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGAssignRandomMeshAttributeSettings", "NodeTitle", "Assign Random Mesh Attribute"); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Param; }
	virtual void GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;
	virtual bool CanDynamicallyTrackKeys() const override { return true; }
#endif

	virtual EPCGDataType GetCurrentPinTypes(const UPCGPin* InPin) const override;
	virtual FString GetAdditionalTitleInformation() const override;

protected:
#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& InPropertyName) const override { return Super::GetChangeTypeForProperty(InPropertyName) | EPCGChangeType::Cosmetic; }
#endif
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	// The name of the row to draw the mesh selection from
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName RowName = NAME_None;	

	// The data table to draw from
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, PCG_OverrideAliases = "PathOverride"))
	TSoftObjectPtr<UDataTable> DataTable;

	/** By default, data table loading is asynchronous, can force it synchronous if needed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Debug")
	bool bSynchronousLoad = false;

	// The name of the attribute to store the mesh selection in
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName AttributeName = TEXT("Mesh");	
};

struct FPCGAssignRandomMeshAttributeContext : public FPCGContext, public IPCGAsyncLoadingContext {};

class FPCGAssignRandomMeshAttribute : public IPCGElementWithCustomContext<FPCGAssignRandomMeshAttributeContext>
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
