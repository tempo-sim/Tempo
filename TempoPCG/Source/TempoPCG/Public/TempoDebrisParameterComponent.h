// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Engine/DataTable.h"

#include "TempoDebrisParameterComponent.generated.h"

USTRUCT(BlueprintType)
struct TEMPOPCG_API FTempoDebrisParameters: public FTableRowBase
{
	GENERATED_BODY()

	// Whether to enable this type of debris.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = 1.0;

	// The density of individual instances in instances per *square meter*.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition=bEnabled, EditConditionHides=true))
	int32 Seed = 42;

	// The density of individual instances in instances per *square meter*.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.01, UIMax=100.0, ClampMin=0.01, ClampMax=100.0, EditCondition=bEnabled, EditConditionHides=true))
	float InstanceDensity = 1.0;

	// The smallest random scale to apply to instances.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.1, UIMax=10.0, ClampMin=0.01, ClampMax=100.0, EditCondition=bEnabled, EditConditionHides=true))
	float MinInstanceScale = 1.0;

	// The largest random scale to apply to instances.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.1, UIMax=10.0, ClampMin=0.01, ClampMax=100.0, EditCondition=bEnabled, EditConditionHides=true))
	float MaxInstanceScale = 1.0;

	// Whether to enable clustering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bClusteringEnabled = false;

	// The mean spacing between clusters *in meters*.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.5, UIMax=50, ClampMin=0.5, ClampMax=50.0, EditCondition="bEnabled && bClusteringEnabled", EditConditionHides=true))
	float ClusterSpacing = 1.0;

	// How abruptly the instance density falls off at the cluster edges. 1.0 is immediate, 5.0 is very gradual.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=1.0, UIMax=5.0, ClampMin=1.0, ClampMax=5.0, EditCondition="bEnabled && bClusteringEnabled", EditConditionHides=true))
	float ClusterFalloff = 1.0;

	// The smallest possible cluster radius *in meters*.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.5, UIMax=5.0, ClampMin=0.5, ClampMax=5.0, EditCondition="bEnabled && bClusteringEnabled", EditConditionHides=true))
	float MinClusterSize = 0.5;

	// The largest possible cluster radius *in meters*.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.5, UIMax=5.0, ClampMin=0.5, ClampMax=5.0, EditCondition="bEnabled && bClusteringEnabled", EditConditionHides=true))
	float MaxClusterSize = 1.5;

	// Whether to enable center keepout.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCenterKeepoutEnabled = false;

	// The portion of the CenterKeepoutWidth to use.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.0, UIMax=1.0, ClampMin=0.0, ClampMax=1.0, EditCondition="bEnabled && bCenterKeepoutEnabled", EditConditionHides=true))
	float CenterKeepoutPortion = 1.0;

	// How abruptly the instance density falls off at the center width. 1.0 is immediate, 5.0 is very gradual.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=1.0, UIMax=5.0, ClampMin=1.0, ClampMax=5.0, EditCondition="bEnabled && bCenterKeepoutEnabled", EditConditionHides=true))
	float CenterKeepoutFalloff = 1.0;

	// The name of the mesh distribution to draw from. Must match the name of a row in the MeshDataTable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName MeshDistribution;

	// The name of the PCG component these settings should control, which should have a PCGDebris graph.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName TargetPCGComponentTag;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOPCG_API UTempoDebrisParameterComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	virtual void SyncPCGParameters(const FTempoDebrisParameters& Params) const;

	// The data table with the mesh distributions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UDataTable> MeshDistributionDataTable;

	// Whether to limit sampling to along the center spline.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSampleAlongCenterSpline = false;

	// Whether to perform center keepout on splines from all actors or only the parent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCenterKeepoutAllActors = true;

	// The indices of splines on the parent actor to be used for center spline sampling (if enabled)
	// and/or on all actors overlapping the parent actor for center keepout (':' for all)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString CenterSplineIndices = TEXT(":");
	
	// The total width around a center spline *in meters* to remove instances, if enabled.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.0, UIMax=50.0, ClampMin=0.0, ClampMax=50.0))
	float CenterKeepoutWidth = 10.0;
};
