// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDebrisParameterComponent.h"

#include "TempoDebris.h"

#include "PCGComponent.h"
#include "PCGGraph.h"
#include "TempoConversion.h"

template <typename T>
bool MaybeSetGraphParameter(UPCGGraphInterface* GraphInterface, const FName& ParameterName, const T& Value)
{
	TValueOrError<T, EPropertyBagResult> ValueOrError = GraphInterface->GetGraphParameter<T>(ParameterName);
	if (!ValueOrError.HasValue())
	{
		UE_LOG(LogTempoDebris, Error, TEXT("Error while getting graph property value: %d"), ValueOrError.GetError())
		return false;
	}
	if (ValueOrError.GetValue() == Value)
	{
		return false;
	}
	EPropertyBagResult SetResult = GraphInterface->SetGraphParameter(ParameterName, Value);
	if (SetResult != EPropertyBagResult::Success)
	{
		UE_LOG(LogTempoDebris, Error, TEXT("Error while setting graph property value: %d"), SetResult)
		return false;
	}
	return true;
}

// Some of the GetGraphParameter specializations return a T*, others return a T.
template <typename T>
bool MaybeSetGraphParameterPtr(UPCGGraphInterface* GraphInterface, const FName& ParameterName, const T& Value)
{
	TValueOrError<T*, EPropertyBagResult> ValueOrError = GraphInterface->GetGraphParameter<T*>(ParameterName);
	if (!ValueOrError.HasValue())
	{
		UE_LOG(LogTempoDebris, Error, TEXT("Error while getting graph property value: %d"), ValueOrError.GetError())
		return false;
	}
	if (*ValueOrError.GetValue() == Value)
	{
		return false;
	}
	EPropertyBagResult SetResult = GraphInterface->SetGraphParameter(ParameterName, Value);
	if (SetResult != EPropertyBagResult::Success)
	{
		UE_LOG(LogTempoDebris, Error, TEXT("Error while setting graph property value: %d"), SetResult)
		return false;
	}
	return true;
}

void UTempoDebrisParameterComponent::SyncPCGParameters(const FTempoDebrisParameters& Params) const
{
	if (!GetOwner())
	{
		return;
	}

	TArray<UPCGComponent*> AllPCGComponents;
	GetOwner()->GetComponents<UPCGComponent>(AllPCGComponents);

	TArray<UPCGComponent*> TargetedPCGComponents = AllPCGComponents.FilterByPredicate([Tag = Params.TargetPCGComponentTag](const UPCGComponent* PCGComponent)
	{
		return PCGComponent->ComponentHasTag(Tag);
	});
	for (UPCGComponent* TargetedPCGComponent : TargetedPCGComponents)
	{
		UPCGGraphInstance* GraphInstance = TargetedPCGComponent->GetGraphInstance();
		if (!GraphInstance)
		{
			UE_LOG(LogTempoDebris, Warning, TEXT("No PCG graph instance on PCG component with tag %s in TempoDebrisParameterComponent"), *Params.TargetPCGComponentTag.ToString());
		}

		bool bAnyParametersUpdated = false;

		if (TargetedPCGComponent->Seed != Params.Seed)
		{
			TargetedPCGComponent->Seed = Params.Seed;
			bAnyParametersUpdated = true;
		}

		if (TargetedPCGComponent->bActivated != Params.bEnabled)
		{
			TargetedPCGComponent->bActivated = Params.bEnabled;
			bAnyParametersUpdated = true;
		}

		const double NominalClusterSize = (Params.MaxClusterSize + Params.MinClusterSize) / 2.0;
		const FVector NominalClusterOffset(NominalClusterSize / 2.0, NominalClusterSize / 2.0, 0.0);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("InstanceDensity"), Params.InstanceDensity);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("MinInstanceScale"), Params.MinInstanceScale);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("MaxInstanceScale"), Params.MaxInstanceScale);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("Clustering"), Params.bClusteringEnabled);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("ClusterDensity"), 1.0 / QuantityConverter<M2CM>::Convert(Params.ClusterSpacing));
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("ClusterSize"), QuantityConverter<M2CM>::Convert(NominalClusterSize));
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("ClusterScaleMin"), Params.MinClusterSize / NominalClusterSize);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("ClusterScaleMax"), Params.MaxClusterSize / NominalClusterSize);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("ClusterFalloffNoise"), 1.0 / Params.ClusterFalloff);
		bAnyParametersUpdated |= MaybeSetGraphParameterPtr(GraphInstance, TEXT("MinClusterOffset"), -QuantityConverter<M2CM>::Convert(NominalClusterOffset));
		bAnyParametersUpdated |= MaybeSetGraphParameterPtr(GraphInstance, TEXT("MaxClusterOffset"), QuantityConverter<M2CM>::Convert(NominalClusterOffset));
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("CenterKeepout"), Params.bCenterKeepoutEnabled);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("CenterFalloffNoise"), Params.CenterKeepoutFalloff);
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("HalfCenterWidth"), Params.CenterKeepoutPortion * QuantityConverter<M2CM>::Convert(CenterKeepoutWidth / 2.0));
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("MeshDistributionName"), Params.MeshDistribution);
		bAnyParametersUpdated |= MaybeSetGraphParameterPtr(GraphInstance, TEXT("MeshDistributionDataTable"), MeshDistributionDataTable.ToSoftObjectPath());
		bAnyParametersUpdated |= MaybeSetGraphParameter(GraphInstance, TEXT("CenterSplineTag"), CenterSplineTag);

		// In the Editor or PIE this is taken care of for us in the PCGComponent.
		// In the packaged game we must trigger regeneration ourselves.
		// But only regenerate if parameters have actually changed, or we've never generated.
#if !WITH_EDITOR
		if (bAnyParametersUpdated || !TargetedPCGComponent->bGenerated)
		{
			TargetedPCGComponent->GenerateLocal(true);
		}
#endif
	}
}
