// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDebrisParameterComponent.h"

#include "TempoDebris.h"

#include "PCGComponent.h"
#include "PCGGraph.h"
#include "TempoConversion.h"

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

		TargetedPCGComponent->Seed = Params.Seed;
		TargetedPCGComponent->bActivated = Params.bEnabled;

		const float NominalClusterSize = (Params.MaxClusterSize + Params.MinClusterSize) / 2.0;
		const FVector NominalClusterOffset(NominalClusterSize / 2.0, NominalClusterSize / 2.0, 0.0);
		GraphInstance->SetGraphParameter(TEXT("InstanceDensity"), Params.InstanceDensity);
		GraphInstance->SetGraphParameter(TEXT("MinInstanceScale"), Params.MinInstanceScale);
		GraphInstance->SetGraphParameter(TEXT("MaxInstanceScale"), Params.MaxInstanceScale);
		GraphInstance->SetGraphParameter(TEXT("Clustering"), Params.bClusteringEnabled);
		GraphInstance->SetGraphParameter(TEXT("ClusterDensity"), 1.0 / QuantityConverter<M2CM>::Convert(Params.ClusterSpacing));
		GraphInstance->SetGraphParameter(TEXT("ClusterSize"), QuantityConverter<M2CM>::Convert(NominalClusterSize));
		GraphInstance->SetGraphParameter(TEXT("ClusterScaleMin"), Params.MinClusterSize / NominalClusterSize);
		GraphInstance->SetGraphParameter(TEXT("ClusterScaleMax"), Params.MaxClusterSize / NominalClusterSize);
		GraphInstance->SetGraphParameter(TEXT("ClusterFalloffNoise"), 1.0 / Params.ClusterFalloff);
		GraphInstance->SetGraphParameter(TEXT("MinClusterOffset"), -QuantityConverter<M2CM>::Convert(NominalClusterOffset));
		GraphInstance->SetGraphParameter(TEXT("MaxClusterOffset"), QuantityConverter<M2CM>::Convert(NominalClusterOffset));
		GraphInstance->SetGraphParameter(TEXT("CenterKeepout"), Params.bCenterKeepoutEnabled);
		GraphInstance->SetGraphParameter(TEXT("CenterFalloffNoise"), Params.CenterKeepoutFalloff);
		GraphInstance->SetGraphParameter(TEXT("HalfCenterWidth"), Params.CenterKeepoutPortion * QuantityConverter<M2CM>::Convert(CenterKeepoutWidth / 2.0));
		GraphInstance->SetGraphParameter(TEXT("MeshDistributionName"), Params.MeshDistribution);
		GraphInstance->SetGraphParameter(TEXT("MeshDistributionDataTable"), MeshDistributionDataTable.ToSoftObjectPath());
		GraphInstance->SetGraphParameter(TEXT("CenterSplineTag"), CenterSplineTag);
	}
}
