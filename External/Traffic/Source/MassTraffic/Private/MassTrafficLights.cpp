// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficLights.h"
#include "MassTraffic.h"
#include "MassTrafficDebugHelpers.h"
#if ENABLE_DRAW_DEBUG
#include "Engine/World.h"
#endif // ENABLE_DRAW_DEBUG
#if WITH_EDITOR
#include "Engine/StaticMesh.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "PointCloudView.h"


FVector TransformPositionFromHoudini(const FVector& HoudiniPosition)
{
	static const FTransform HoudiniToUEConversionTransform(FRotator(0.0f, 0.0f, -90.0f), FVector::ZeroVector, FVector(1.0f, 1.0f, -1.0f));
	return HoudiniToUEConversionTransform.TransformPosition(HoudiniPosition);
}

FQuat TransformRotationFromHoudini(const FQuat& HoudiniRotation)
{
	return FQuat(HoudiniRotation.X, HoudiniRotation.Z, -HoudiniRotation.Y, HoudiniRotation.W);
}

void UMassTrafficLightInstancesDataAsset::PopulateTrafficLightsFromPointCloud()
{
	TrafficLights.Empty();
	NumTrafficLights = 0;

	if (TrafficLightsPointCloud.IsNull())
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - No TrafficLightsPointCloud point cloud is set."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	// Load point cloud
	UPointCloud* LoadedTrafficLightsPointCloud = TrafficLightsPointCloud.LoadSynchronous();
	if (!LoadedTrafficLightsPointCloud)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Couldn't load TrafficLightsPointCloud %s."), ANSI_TO_TCHAR(__FUNCTION__), *TrafficLightsPointCloud.ToString());
		return;
	}

	UPointCloudView* TrafficLightPointCloudView = LoadedTrafficLightsPointCloud->MakeView();
	if (!TrafficLightPointCloudView) 
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - TrafficLightsPointCloud is valid, but could not create Point Cloud View"), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}
	
	FScopedSlowTask SlowTask(LoadedTrafficLightsPointCloud->GetCount(), NSLOCTEXT("MassTraffic", "PopulateTrafficLightsFromPointCloud", "Reading points from TrafficLightsPointCloud ..."));
	SlowTask.MakeDialog(true);

	// Read the TrafficLightConfiguration to build a map of Mesh to VehicleTypeIndex. It's the
	// static mesh path that should be specified in the parked vehicle PointCloud as each points unreal_instance.
	if (!TrafficLightTypesData)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("No TrafficLightTypesData set on %s. Please specify one to use for matching parking space 'unreal_instance' to vehicle type index."), *GetName())
		return;
	}
	SlowTask.FrameMessage = NSLOCTEXT("MassTraffic", "ReadingTrafficLightTypesData", "Reading TrafficLightTypesData to match traffic light types against ...");
	TMap<FString, uint8> UnrealInstanceToTrafficLightTypeIndex;
	for (uint8 TrafficLightTypeIndex = 0; TrafficLightTypeIndex < TrafficLightTypesData->TrafficLightTypes.Num(); ++TrafficLightTypeIndex)
	{
		const FMassTrafficLightTypeData& LightType = TrafficLightTypesData->TrafficLightTypes[TrafficLightTypeIndex];
		for (const FMassStaticMeshInstanceVisualizationMeshDesc& Mesh : LightType.StaticMeshInstanceDesc.Meshes)
		{
			if (Mesh.Mesh)
			{
				FString MeshPathName = Mesh.Mesh->GetPathName();
				UnrealInstanceToTrafficLightTypeIndex.Add(MeshPathName, TrafficLightTypeIndex);
			}
		}
	}
	SlowTask.FrameMessage = FText::GetEmpty();

	// Get all transforms and IDs.
	TArray<TPair<int32, FTransform>> TransformsAndIds = TrafficLightPointCloudView->GetPerIdTransforms();

	// Get traffic light locations
	for (const auto& Entry : TransformsAndIds)
	{
		// Just for clarity.
		const int32& ID(Entry.Key);
		const FTransform& Transform(Entry.Value);

		SlowTask.EnterProgressFrame();

		if (SlowTask.ShouldCancel())
		{
			TrafficLights.Empty();
			NumTrafficLights = 0;
			return;
		}

		const TMap<FString, FString>& Metadata = TrafficLightPointCloudView->GetMetadata(ID);

		bool bHasTrafficLight = false;
		if (Metadata.Contains(TEXT("has_traffic_light")))
		{
			const FString& HasTrafficLightString = Metadata[TEXT("has_traffic_light")];
			int Tmp = 0;
			if (!FDefaultValueHelper::ParseInt(HasTrafficLightString, Tmp))
			{
				UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not parse int from string has_traffic_light='%s' in Rule Processor Point Cloud '%s'."), ANSI_TO_TCHAR(__FUNCTION__), *HasTrafficLightString, *TrafficLightsPointCloud.ToString());
				continue;
			}
			bHasTrafficLight = !!Tmp;		
		}
		
		if (!bHasTrafficLight)
		{
			continue;
		}

		
		auto GetStringFromStringMap = [&](FString ValueName, FString& Value) -> bool
		{
			if (Metadata.Contains(ValueName))
			{
				Value = Metadata[ValueName];
				return true;
			}
			else
			{
				UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not find value '%s' in string map."), ANSI_TO_TCHAR(__FUNCTION__), *ValueName);
				return false;
			}
		};

		auto GetFloatFromStringMap = [&](FString ValueName, double& Value) -> bool
		{
			FString ValueString;
			if (GetStringFromStringMap(ValueName, ValueString))
			{
				if (FDefaultValueHelper::ParseDouble(ValueString, Value))
				{
					return true;
				}
				else
				{
					UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not parse double for for value '%s'='%s' in string map."), ANSI_TO_TCHAR(__FUNCTION__), *ValueName, *ValueString);
					return false;
				}
			}

			return false;
		};

		
		FVector TrafficLightPosition = FVector::ZeroVector;
		{
			if (!(GetFloatFromStringMap("traffic_light.0", TrafficLightPosition.X) || GetFloatFromStringMap("traffic_lightx", TrafficLightPosition.X))) continue;
			if (!(GetFloatFromStringMap("traffic_light.1", TrafficLightPosition.Y) || GetFloatFromStringMap("traffic_lighty", TrafficLightPosition.Y))) continue;
			if (!(GetFloatFromStringMap("traffic_light.2", TrafficLightPosition.Z) || GetFloatFromStringMap("traffic_lightz", TrafficLightPosition.Z))) continue;

			if (bApplyHoudiniToUETransformToTrafficLights)
			{
				TrafficLightPosition = TransformPositionFromHoudini(TrafficLightPosition);
			}
		}
		
		FQuat TrafficLightRotation = FQuat::Identity;
		{
			if (!(GetFloatFromStringMap("traffic_light_orient.0", TrafficLightRotation.X) || GetFloatFromStringMap("traffic_light_orientx", TrafficLightRotation.X))) continue;
			if (!(GetFloatFromStringMap("traffic_light_orient.1", TrafficLightRotation.Y) || GetFloatFromStringMap("traffic_light_orienty", TrafficLightRotation.Y))) continue;
			if (!(GetFloatFromStringMap("traffic_light_orient.2", TrafficLightRotation.Z) || GetFloatFromStringMap("traffic_light_orientz", TrafficLightRotation.Z))) continue;
			if (!(GetFloatFromStringMap("traffic_light_orient.3", TrafficLightRotation.W) || GetFloatFromStringMap("traffic_light_orientw", TrafficLightRotation.W))) continue;

			if (bApplyHoudiniToUETransformToTrafficLights)
			{
				TrafficLightRotation = TransformRotationFromHoudini(TrafficLightRotation);
			}
		}

		// Get traffic light type for unreal_instance
		FString UnrealInstance;
		if (!GetStringFromStringMap("unreal_instance", UnrealInstance)) continue;
		
		// Chop off StaticMesh' from start and ' from end to match against pure path name
		if (UnrealInstance.RemoveFromStart(TEXT("StaticMesh'")))
		{
			UnrealInstance.RemoveFromEnd(TEXT("'"));
		}

		uint8 TrafficLightTypeIndex; 
		if (uint8* TrafficLightIndexForUnrealInstance = UnrealInstanceToTrafficLightTypeIndex.Find(UnrealInstance))
		{
			TrafficLightTypeIndex = *TrafficLightIndexForUnrealInstance;
		}
		else
		{
			UE_LOG(LogMassTraffic, Warning, TEXT("Couldn't find matching traffic traffic light type for unreal_instance: %s. Using a random traffic light type instead."), *UnrealInstance);
			TrafficLightTypeIndex = RandomStream.RandHelper(NumTrafficLights);
		}

		
		// The transforms describe the center points of intersection sides.
		// The transform has already been converted from Houdini to UE by RuleProcessor.
		const FVector ControlledIntersectionSideMidpoint = Transform.GetLocation();

		float TrafficLightZRotation = 0.0f;
		FVector TrafficLightXDirection_Debug = FVector::ZeroVector;
		{
			const FRotator Rotator(TrafficLightRotation);
			const FVector Euler = Rotator.Euler();
			TrafficLightZRotation = Euler.Z;
			TrafficLightXDirection_Debug = Rotator.RotateVector(FVector::XAxisVector);
		}
		
		const FMassTrafficLightInstanceDesc TrafficLightDetail(TrafficLightPosition, TrafficLightZRotation, ControlledIntersectionSideMidpoint, TrafficLightTypeIndex);
		TrafficLights.Add(TrafficLightDetail);

		// DEBUG
		#if ENABLE_DRAW_DEBUG
		if (GDebugMassTraffic)
		{
			UE::MassTraffic::DrawDebugTrafficLight(GWorld, TrafficLightPosition, TrafficLightXDirection_Debug, &ControlledIntersectionSideMidpoint, FColor::Yellow, FColor::Yellow, FColor::Yellow, FColor::Yellow, false, 20.0f);
		}
		#endif
	}

	NumTrafficLights = TrafficLights.Num();

	// Dirty the actor
	Modify();
}

#endif
