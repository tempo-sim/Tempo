// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficParkedVehicles.h"
#include "MassCommonUtils.h"
#include "MassTraffic.h"
#include "MassTrafficDebugHelpers.h"
#include "Algo/RandomShuffle.h"
#if ENABLE_DRAW_DEBUG && WITH_MASSTRAFFIC_DEBUG
#include "Engine/World.h"
#endif //ENABLE_DRAW_DEBUG
#if WITH_EDITOR
#include "Misc/DefaultValueHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "PointCloudView.h"


void UMassTrafficParkingSpacesDataAsset::PopulateParkingSpacesFromPointCloud()
{
	TypedParkingSpaces.Empty();

	if (ParkingSpacesPointCloud.IsNull())
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - No ParkingSpacesPointCloud point cloud is set."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	// Load point cloud
	UPointCloud* LoadedParkingSpacesPointCloud = ParkingSpacesPointCloud.LoadSynchronous();
	if (!LoadedParkingSpacesPointCloud)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Couldn't load ParkingSpacesPointCloud %s."), ANSI_TO_TCHAR(__FUNCTION__), *ParkingSpacesPointCloud.ToString());
		return;
	}

	UPointCloudView* ParkingSpacesPointCloudView = LoadedParkingSpacesPointCloud->MakeView();
	if (!ParkingSpacesPointCloudView) 
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - ParkingSpacesPointCloud is valid, but could not create Point Cloud View"), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}
	
	FScopedSlowTask SlowTask(LoadedParkingSpacesPointCloud->GetCount() + 1, NSLOCTEXT("MassTraffic", "PopulateParkingSpacesFromPointCloud", "Reading points from ParkingSpacesPointCloud ..."));
	SlowTask.MakeDialog(true);

	// Get all transforms
	TArray<FTransform> Transforms;
	TArray<int32> IDs;
	ParkingSpacesPointCloudView->GetTransformsAndIds(Transforms, IDs);

	if(IDs.Num() != Transforms.Num())
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Point Cloud View GetTransformsAndIds returned invalid data"), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}
	
	// Get parked vehicle transform & type
	int TotalParkingSpaces = 0;
	for(int32 PointIndex = 0; PointIndex < IDs.Num(); ++PointIndex)
	{
		SlowTask.EnterProgressFrame();

		// Cancel?
		if (SlowTask.ShouldCancel())
		{
			TypedParkingSpaces.Empty();
			return;
		}

		int32 ID = IDs[PointIndex];
		const FTransform& Transform = Transforms[PointIndex];

		const TMap<FString, FString>& Metadata = ParkingSpacesPointCloudView->GetMetadata(ID);

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
		
		FString Type;
		GetStringFromStringMap("type", Type);
		if (Type == TEXT("cars"))
		{
			FString UnrealInstance;
			GetStringFromStringMap("unreal_instance", UnrealInstance);

			// Chop off StaticMesh' from start and ' from end to match against pure path name
			if (UnrealInstance.RemoveFromStart(TEXT("StaticMesh'")))
			{
				UnrealInstance.RemoveFromEnd(TEXT("'"));
			}

			FName ParkingSpaceTypeName;
			if (const FName* ParkingSpaceTypeNameForUnrealInstance = UnrealInstanceToParkingSpaceTypeName.Find(UnrealInstance))
			{
				ParkingSpaceTypeName = *ParkingSpaceTypeNameForUnrealInstance;
			}
			else
			{
				UE_LOG(LogMassTraffic, Warning, TEXT("Couldn't find matching traffic vehicle type for unreal_instance: %s. Using default parking space type (%s) instead."), *UnrealInstance, *DefaultParkingSpaceType.ToString());
				SlowTask.FrameMessage = FText::Format(NSLOCTEXT("MassTraffic", "CouldntFindUnrealInstance", "Couldn't find matching traffic vehicle type for unreal_instance: {0}. Using default parking space type {1} instead."), FText::FromString(UnrealInstance), FText::FromName(DefaultParkingSpaceType));
				
				ParkingSpaceTypeName = DefaultParkingSpaceType;
			}

			// Find or add parking spaces for type
			FMassTrafficTypedParkingSpaces* TypedParkingSpacesDesc = TypedParkingSpaces.FindByPredicate([&ParkingSpaceTypeName](const FMassTrafficTypedParkingSpaces& TypedParkingSpace){ return TypedParkingSpace.Name == ParkingSpaceTypeName; });
			if (!TypedParkingSpacesDesc)
			{
				TypedParkingSpacesDesc = &TypedParkingSpaces.AddDefaulted_GetRef();
				TypedParkingSpacesDesc->Name = ParkingSpaceTypeName;
			}
			
			TypedParkingSpacesDesc->ParkingSpaces.Add(Transform);
			++TotalParkingSpaces;
			SlowTask.FrameMessage = FText::Format(NSLOCTEXT("MassTraffic", "FoundNumParkingSpaces", "Found {0} parking spaces so far ..."), TotalParkingSpaces);

			#if ENABLE_DRAW_DEBUG && WITH_MASSTRAFFIC_DEBUG
			if (GDebugMassTraffic)
			{
				UE::MassTraffic::DrawDebugParkingSpace(GWorld, Transform.GetLocation(), Transform.GetRotation(), /*Color*/UE::MassTraffic::PointerToColor(TypedParkingSpacesDesc));
			}
			#endif
		}
	}

	// Randomly shuffle the parking space transforms so we can select the first NumParkedVehicles and get a random
	// distribution of parking spaces 
	if (bShuffleParkingSpaces && !UE::Mass::Utils::IsDeterministic())
	{
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("MassTraffic", "ShufflingParkingSpaces", "Shuffling parking space transforms ..."));
		for (FMassTrafficTypedParkingSpaces& TypedParkingSpacesDesc : TypedParkingSpaces)
		{
			Algo::RandomShuffle(TypedParkingSpacesDesc.ParkingSpaces);
		}
	}

	// Update counts
	NumParkingSpaces = 0;
	for (FMassTrafficTypedParkingSpaces& TypedParkingSpacesDesc : TypedParkingSpaces)
	{
		TypedParkingSpacesDesc.NumParkingSpaces = TypedParkingSpacesDesc.ParkingSpaces.Num();
		NumParkingSpaces += TypedParkingSpacesDesc.NumParkingSpaces;
	}

	// Dirty the actor
	MarkPackageDirty();
}

#endif // WITH_EDITOR
