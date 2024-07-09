// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "MassTrafficInstancePlaybackHelpers.generated.h"

// Use floats to match custom floats of instanced static mesh
// We could pack a float w/ more parameters if desired
USTRUCT(BlueprintType)
struct FMassTrafficAnimState
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassTraffic")
	float StartFrame = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassTraffic")
	float NumFrames = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassTraffic")
	float PlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassTraffic")
	float bLooping = 1.0f; 

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassTraffic")
	float GlobalStartTime = 0.0f; 
};

USTRUCT(BlueprintType)
struct FMassTrafficInstancePlaybackData
{
	GENERATED_USTRUCT_BODY()

	// Store prev state to allow blending of prev->current state in material
	// Uncomment this if we start blending states
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassTraffic")
	//FMassTrafficAnimState PrevState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassTraffic")
	FMassTrafficAnimState CurrentState;
};

USTRUCT(BlueprintType)
struct FMassTrafficAnimationSyncData
{
	GENERATED_USTRUCT_BODY()

	// The time used for sync when transitioning from skeletal mesh to material animated static mesh.
	// World real time at the time of the transition
	float SyncTime;
};

USTRUCT(BlueprintType)
struct FMassTrafficInstanceData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<FMassTrafficInstancePlaybackData> PlaybackData;

	UPROPERTY()
	TArray<FInstancedStaticMeshInstanceData> StaticMeshInstanceData;
};

// class UMassTrafficDataAsset;

UCLASS()
class MASSTRAFFIC_API UMassTrafficInstancePlaybackLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	//UFUNCTION(BlueprintCallable, Category = "MassTraffic|Playback")
	//static void SetupInstancedMeshComponent(UInstancedStaticMeshComponent* InstancedMeshComponent, UPARAM(ref) FMassTrafficInstanceData& InstanceData, int32 NumInstances);

	//UFUNCTION(BlueprintCallable, Category = "MassTraffic|Playback")
	//static void BatchUpdateInstancedMeshComponent(UInstancedStaticMeshComponent* InstancedMeshComponent, UPARAM(ref) FMassTrafficInstanceData& InstanceData);

	//UFUNCTION(BlueprintCallable, Category = "MassTraffic|Playback")
	//static void AllocateInstanceData(UPARAM(ref) FMassTrafficInstanceData& InstanceData, int32 Count);

	//UFUNCTION(BlueprintCallable, Category = "MassTraffic|Playback")
	//static bool UpdateInstanceData(UPARAM(ref) FMassTrafficInstanceData& InstanceData, int32 InstanceIndex, const FMassTrafficInstancePlaybackData& PlaybackData, const FTransform& Transform);

	//UFUNCTION(BlueprintCallable, Category = "MassTraffic|Playback")
	//static bool GetInstancePlaybackData(UPARAM(ref) const FMassTrafficInstanceData& InstanceData, int32 InstanceIndex, FMassTrafficInstancePlaybackData& InstancePlaybackData);

	//UFUNCTION(BlueprintCallable, Category = "MassTraffic|Playback")
	//static bool GetInstanceTransform(UPARAM(ref) const FMassTrafficInstanceData& InstanceData, int32 InstanceIndex, FTransform& InstanceTransform);

	UFUNCTION(BlueprintPure, BlueprintCallable, Category = "MassTraffic|Playback")
	static bool AnimStateFromDataAsset(const UAnimToTextureDataAsset* DataAsset, int32 StateIndex, FMassTrafficAnimState& AnimState);
};
