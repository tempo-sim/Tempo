// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "TempoIntersectionControlComponent.h"

#include "MassTrafficSigns.h"
#include "TempoAgentsSettings.h"
#include "TempoAgentsShared.h"
#include "TempoRoadInterface.h"

#include "TempoCoreUtils.h"

void UTempoIntersectionControlComponent::SetupTrafficControllers()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllers - World is invalid."));
		return;
	}

	if (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE)
	{
		SetupTrafficControllerRuntimeData();
	}
	else if (World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::EditorPreview)
	{
		SetupTrafficControllerMeshData();
	}
}

void UTempoIntersectionControlComponent::SetupTrafficControllerMeshData()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerMeshData - OwnerActor is invalid."));
		return;
	}
	
	if (!OwnerActor->Implements<UTempoIntersectionInterface>())
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerMeshData - OwnerActor does not implement UTempoIntersectionInterface."));
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerMeshData - World is invalid."));
		return;
	}

	DestroyTrafficControllerMeshData();

	const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);
	
	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		const FName MeshComponentName = *FString::Printf(TEXT("TrafficController_%d"), ConnectionIndex);
		
		UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(OwnerActor, UStaticMeshComponent::StaticClass(), MeshComponentName);
		if (MeshComponent == nullptr)
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerMeshData - Could not create valid MeshComponent with name: %s."), *MeshComponentName.ToString());
			return;
		}

		const ETempoTrafficControllerType TrafficControllerType = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerType);
		
		FTempoTrafficControllerMeshInfo TrafficControllerMeshInfo = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerMeshInfo, ConnectionIndex, TrafficControllerType);
		const FVector TrafficControllerLocation = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerLocation, ConnectionIndex, TrafficControllerType, ETempoCoordinateSpace::World);
		const FRotator TrafficControllerRotation = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerRotation, ConnectionIndex, TrafficControllerType, ETempoCoordinateSpace::World);

		MeshComponent->SetWorldLocation(TrafficControllerLocation);
		MeshComponent->SetWorldRotation(TrafficControllerRotation);
		MeshComponent->SetWorldScale3D(TrafficControllerMeshInfo.MeshScale);

		MeshComponent->SetStaticMesh(TrafficControllerMeshInfo.TrafficControllerMesh);
		MeshComponent->AttachToComponent(OwnerActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		MeshComponent->RegisterComponent();
		
		OwnerActor->AddInstanceComponent(MeshComponent);
		
		if (TrafficControllerType == ETempoTrafficControllerType::TrafficLight)
		{
			// Hide our Traffic Light Mesh Components in game since the Mass Traffic system will be managing
			// its own Actor for each Intersection, which will have a Static Mesh Component for each Traffic Light.
			// Note:  In the future, we will look into making the Mass Traffic system use our Static Mesh Components,
			// rather than create them again.
			MeshComponent->SetHiddenInGame(true);
		}
	}
}

void UTempoIntersectionControlComponent::SetupTrafficControllerRuntimeData()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerRuntimeData - OwnerActor is invalid."));
		return;
	}
	
	if (!OwnerActor->Implements<UTempoIntersectionInterface>())
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerRuntimeData - OwnerActor does not implement UTempoIntersectionInterface."));
		return;
	}

	const ETempoTrafficControllerType TrafficControllerType = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerType);
	if (TrafficControllerType != ETempoTrafficControllerType::TrafficLight)
	{
		// Currently, only Traffic Lights require additional runtime setup.
		// So, there's nothing to do.
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerRuntimeData - World is invalid."));
		return;
	}

	UMassTrafficControllerRegistrySubsystem* TrafficControllerRegistrySubsystem = World->GetSubsystem<UMassTrafficControllerRegistrySubsystem>();
	if (TrafficControllerRegistrySubsystem == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerRuntimeData - Failed to get TrafficControllerRegistrySubsystem."));
		return;
	}

	const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);
	
	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		FTempoTrafficControllerMeshInfo TrafficControllerMeshInfo = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerMeshInfo, ConnectionIndex, TrafficControllerType);
		const FVector TrafficControllerLocation = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerLocation, ConnectionIndex, TrafficControllerType, ETempoCoordinateSpace::World);
		const FRotator TrafficControllerRotation = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerRotation, ConnectionIndex, TrafficControllerType, ETempoCoordinateSpace::World);
		
		const FVector IntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(OwnerActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, ConnectionIndex, ETempoCoordinateSpace::World);
		
		const AActor* RoadQueryActor = GetConnectedRoadActor(*OwnerActor, ConnectionIndex);
		if (RoadQueryActor == nullptr)
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficControllerRuntimeData - Failed to get Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *OwnerActor->GetName(), ConnectionIndex);
			return;
		}

		constexpr int32 NumTrafficControllerMeshesPerTrafficLightType = 1;
		
		FStaticMeshInstanceVisualizationDesc StaticMeshInstanceVisualizationDesc;
		StaticMeshInstanceVisualizationDesc.Meshes.Reserve(NumTrafficControllerMeshesPerTrafficLightType);

		FMassStaticMeshInstanceVisualizationMeshDesc& MeshDesc = StaticMeshInstanceVisualizationDesc.Meshes.AddDefaulted_GetRef();
		MeshDesc.Mesh = TrafficControllerMeshInfo.TrafficControllerMesh;

		const int32 NumLanes = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetNumTempoLanes);
		
		FMassTrafficLightTypeData TrafficLightType(TrafficControllerMeshInfo.TrafficControllerMesh->GetFName(), StaticMeshInstanceVisualizationDesc, NumLanes);

		const int32 TrafficLightTypeIndex = TrafficLightRegistrySubsystem->RegisterTrafficLightType(TrafficLightType);
		
		FMassTrafficLightInstanceDesc TrafficLightInstanceDesc(TrafficControllerLocation, TrafficControllerRotation.Yaw, IntersectionEntranceLocation, TrafficLightTypeIndex, TrafficControllerMeshInfo.MeshScale);
		
		TrafficLightRegistrySubsystem->RegisterTrafficLight(TrafficLightInstanceDesc);
	}
}

void UTempoIntersectionControlComponent::DestroyTrafficControllerMeshData()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - DestroyTrafficControllers - OwnerActor is invalid."));
		return;
	}
	
	TArray<UStaticMeshComponent*> MeshComponents;
	OwnerActor->GetComponents<UStaticMeshComponent>(MeshComponents);
	
	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		if (MeshComponent != nullptr && MeshComponent->GetName().StartsWith(TEXT("TrafficController_")))
		{
			MeshComponent->DestroyComponent();
		}
	}
}

bool UTempoIntersectionControlComponent::TryGetIntersectionTrafficControllerLocation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoTrafficControllerType TrafficControllerType, const FTempoTrafficControllerMeshInfo& TrafficControllerMeshInfo, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionTrafficControllerLocation) const
{
	if (IntersectionQueryActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - TryGetIntersectionTrafficControllerLocation - IntersectionQueryActor is invalid."));
		return false;
	}
	
	if (TrafficControllerType == ETempoTrafficControllerType::TrafficLight)
	{
		ETempoRoadConfigurationDescriptor TrafficLightAnchorRoadConfigurationDescriptor;
		const int32 TrafficLightAnchorConnectionIndex = GetTrafficLightAnchorConnectionIndex(*IntersectionQueryActor, ConnectionIndex, TrafficLightAnchorRoadConfigurationDescriptor);

		const AActor* TrafficLightAnchorRoadQueryActor = GetConnectedRoadActor(*IntersectionQueryActor, TrafficLightAnchorConnectionIndex);
		if (TrafficLightAnchorRoadQueryActor == nullptr)
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - Failed to get Traffic Light Anchor when determining Traffic Light Location - Failed to get Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor->GetName(), TrafficLightAnchorConnectionIndex);
			return false;
		}
		
		const FVector CurrentIntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, ConnectionIndex, ETempoCoordinateSpace::World);
		const FVector TrafficLightAnchorIntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, TrafficLightAnchorConnectionIndex, ETempoCoordinateSpace::World);

		const FVector TrafficLightAnchorIntersectionEntranceForwardVector = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, TrafficLightAnchorConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();
		const FVector TrafficLightAnchorIntersectionEntranceRightVector = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector, TrafficLightAnchorConnectionIndex, ETempoCoordinateSpace::World);
		const FVector TrafficLightAnchorIntersectionEntranceUpVector = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceUpVector, TrafficLightAnchorConnectionIndex, ETempoCoordinateSpace::World);
		
		const FVector LongitudinalOffset = TrafficLightAnchorIntersectionEntranceForwardVector * TrafficControllerMeshInfo.LongitudinalOffset;
		
		const FVector TrafficLightAnchorRightVector = (TrafficLightAnchorConnectionIndex == ConnectionIndex || TrafficLightAnchorRoadConfigurationDescriptor == ETempoRoadConfigurationDescriptor::RightTurn) ? TrafficLightAnchorIntersectionEntranceRightVector : -TrafficLightAnchorIntersectionEntranceRightVector;
		const float LateralOffsetScalar = GetLateralOffsetFromControlPoint(*TrafficLightAnchorRoadQueryActor, TrafficControllerMeshInfo.LateralOffsetOrigin, TrafficControllerMeshInfo.LateralOffset);
		const FVector LateralOffset = TrafficLightAnchorRightVector * LateralOffsetScalar;

		const FVector VerticalOffset = TrafficLightAnchorIntersectionEntranceUpVector * TrafficControllerMeshInfo.VerticalOffset; 

		const FVector IntersectionTrafficControllerLocation = TrafficLightAnchorIntersectionEntranceLocation + LongitudinalOffset + LateralOffset + VerticalOffset;
		
		OutIntersectionTrafficControllerLocation = IntersectionTrafficControllerLocation;

		if (const UWorld* World = IntersectionQueryActor->GetWorld(); World != nullptr && (World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::EditorPreview))
		{
			DrawDebugDirectionalArrow(World, CurrentIntersectionEntranceLocation, IntersectionTrafficControllerLocation, 50000.0f, FColor::Yellow, false, 20.0f, 0, 10.0f);
			DrawDebugDirectionalArrow(World, CurrentIntersectionEntranceLocation, TrafficLightAnchorIntersectionEntranceLocation, 50000.0f, FColor::Purple, false, 20.0f, 0, 10.0f);
		}
	}
	else
	{
		const AActor* CurrentRoadActorQuery = GetConnectedRoadActor(*IntersectionQueryActor, ConnectionIndex);
		if (CurrentRoadActorQuery == nullptr)
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - Failed to get Connected Road Actor when determining Stop Sign Location - Failed to get Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor->GetName(), ConnectionIndex);
			return false;
		}
		
		const FVector CurrentIntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, ConnectionIndex, ETempoCoordinateSpace::World);
		
		const FVector CurrentIntersectionEntranceForwardVector = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, ConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();
		const FVector CurrentIntersectionEntranceRightVector = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector, ConnectionIndex, ETempoCoordinateSpace::World);
		const FVector CurrentIntersectionEntranceUpVector = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceUpVector, ConnectionIndex, ETempoCoordinateSpace::World);
		
		const FVector LongitudinalOffset = CurrentIntersectionEntranceForwardVector * TrafficControllerMeshInfo.LongitudinalOffset;

		const float LateralOffsetScalar = GetLateralOffsetFromControlPoint(*CurrentRoadActorQuery, TrafficControllerMeshInfo.LateralOffsetOrigin, TrafficControllerMeshInfo.LateralOffset);
		const FVector LateralOffset = CurrentIntersectionEntranceRightVector * LateralOffsetScalar;

		const FVector VerticalOffset = CurrentIntersectionEntranceUpVector * TrafficControllerMeshInfo.VerticalOffset;

		const FVector IntersectionTrafficControllerLocation = CurrentIntersectionEntranceLocation + LongitudinalOffset + LateralOffset + VerticalOffset;
		
		OutIntersectionTrafficControllerLocation = IntersectionTrafficControllerLocation;
	}

	return true;
}

bool UTempoIntersectionControlComponent::TryGetIntersectionTrafficControllerRotation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoTrafficControllerType TrafficControllerType, const FTempoTrafficControllerMeshInfo& TrafficControllerMeshInfo, ETempoCoordinateSpace CoordinateSpace, FRotator& OutIntersectionTrafficControllerRotation) const
{
	if (IntersectionQueryActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - TryGetIntersectionTrafficControllerRotation - IntersectionQueryActor is invalid."));
		return false;
	}

	const FVector TrafficControllerForwardVector = -UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, ConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();
	const FVector TrafficControllerRightVector = -UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector, ConnectionIndex, ETempoCoordinateSpace::World);
	const FVector TrafficControllerUpVector = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceUpVector, ConnectionIndex, ETempoCoordinateSpace::World);

	const FRotator TrafficControllerBaseRotation = FMatrix(TrafficControllerForwardVector, TrafficControllerRightVector, TrafficControllerUpVector, FVector::ZeroVector).Rotator();
	const FRotator RotationOffset = FRotator(0.0f, TrafficControllerMeshInfo.YawOffset, 0.0f);
	
	const FRotator IntersectionTrafficControllerRotation = TrafficControllerBaseRotation + RotationOffset;
	
	OutIntersectionTrafficControllerRotation = IntersectionTrafficControllerRotation;

	return true;
}

bool UTempoIntersectionControlComponent::TryGetRoadConfigurationInfoForIntersection(const AActor* IntersectionQueryActor, int32 SourceConnectionIndex, ETempoRoadConfigurationDescriptor RoadConfigurationDescriptor, TArray<FTempoRoadConfigurationInfo>& OutRoadConfigurationInfos) const
{
	if (IntersectionQueryActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - TryGetRoadConfigurationInfoForIntersection - IntersectionQueryActor is invalid."));
		return false;
	}

	TArray<FTempoRoadConfigurationInfo> RoadConfigurationInfos;
	
	if (RoadConfigurationDescriptor == ETempoRoadConfigurationDescriptor::ThroughRoad)
	{
		int32 DestConnectionIndex = -1;
		float ConnectionDotProduct = -1.0f;
		if (!TryGetStraightestConnectionIndexForIntersection(IntersectionQueryActor, SourceConnectionIndex,DestConnectionIndex, ConnectionDotProduct))
		{
			return false;
		}
		
		FTempoRoadConfigurationInfo ThroughRoadConfigurationInfo(SourceConnectionIndex, DestConnectionIndex, ConnectionDotProduct, ETempoRoadConfigurationDescriptor::ThroughRoad);
		RoadConfigurationInfos.Add(ThroughRoadConfigurationInfo);

		OutRoadConfigurationInfos = RoadConfigurationInfos;
		return true;
	}
	else
	{
		const UTempoAgentsSettings* TempoAgentsSettings = GetDefault<UTempoAgentsSettings>();
		if (TempoAgentsSettings == nullptr)
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - TryGetRoadConfigurationInfoForIntersection - Can't access TempoAgentsSettings.  Using hard-coded default settings."));
		}
		
		const float MaxThroughRoadAngleDegrees = TempoAgentsSettings != nullptr ? TempoAgentsSettings->GetMaxThroughRoadAngleDegrees() : 15.0f;
		const float MaxThroughRoadAngleRadians = FMath::DegreesToRadians(MaxThroughRoadAngleDegrees);
		
		const FVector SourceEntranceTangentInWorldFrame = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, SourceConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();

		const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);

		for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
		{
			// Skip the source connection.  We're only processing destination connections.
			if (ConnectionIndex == SourceConnectionIndex)
			{
				continue;
			}

			const FVector DestinationEntranceTangentInWorldFrame = -UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, ConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();

			const float ForwardDotProduct = SourceEntranceTangentInWorldFrame.Dot(DestinationEntranceTangentInWorldFrame);
			const bool bIsThroughRoad = ForwardDotProduct > FMath::Cos(MaxThroughRoadAngleRadians);

			const auto& FoundDesiredConnectionType = [&]()
			{
				switch(RoadConfigurationDescriptor)
				{
				case ETempoRoadConfigurationDescriptor::ThroughRoad:
					return bIsThroughRoad;
				case ETempoRoadConfigurationDescriptor::LeftTurn:
					// Intentional fallthrough.
				case ETempoRoadConfigurationDescriptor::RightTurn:
					{
						const FVector SourceEntranceRightVectorInWorldFrame = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector, SourceConnectionIndex, ETempoCoordinateSpace::World);
						const float RightDotProduct = SourceEntranceRightVectorInWorldFrame.Dot(DestinationEntranceTangentInWorldFrame);
						
						return !bIsThroughRoad && ((RoadConfigurationDescriptor == ETempoRoadConfigurationDescriptor::RightTurn && RightDotProduct > 0.0f) || (RoadConfigurationDescriptor == ETempoRoadConfigurationDescriptor::LeftTurn && RightDotProduct < 0.0f));
					}
				default:
					ensureMsgf(false, TEXT("Unsupported ETempoRoadConfigurationDescriptor type.  Will not add connection to RoadConfigurationInfos."));
					return false;
				}	
			};

			if (FoundDesiredConnectionType())
			{
				FTempoRoadConfigurationInfo ThroughRoadConfigurationInfo(SourceConnectionIndex, ConnectionIndex, ForwardDotProduct, RoadConfigurationDescriptor);
				RoadConfigurationInfos.Add(ThroughRoadConfigurationInfo);
			}
		}

		if (RoadConfigurationInfos.Num() <= 0)
		{
			return false;
		}
		
		OutRoadConfigurationInfos = RoadConfigurationInfos;
		return true;
	}
}

bool UTempoIntersectionControlComponent::TryGetStraightestConnectionIndexForIntersection(const AActor* IntersectionQueryActor, int32 SourceConnectionIndex, int32& OutDestConnectionIndex, float& OutConnectionDotProduct) const
{
	if (IntersectionQueryActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - GetStraightestConnectionIndexForIntersection - IntersectionQueryActor is invalid."));
		return false;
	}
	
    float MaxDotProduct = -1.0f;
    int32 StraightestRoadActorConnectionIndex = -1;

    const FVector SourceEntranceTangentInWorldFrame = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, SourceConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();

    const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);

    for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
    {
		// Skip the source connection.  We're only processing destination connections.
		if (ConnectionIndex == SourceConnectionIndex)
		{
		    continue;
		}

		const FVector DestinationEntranceTangentInWorldFrame = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, ConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();

		const float DotProduct = SourceEntranceTangentInWorldFrame.Dot(-DestinationEntranceTangentInWorldFrame);

		if (DotProduct > MaxDotProduct)
		{
			MaxDotProduct = DotProduct;
			StraightestRoadActorConnectionIndex = ConnectionIndex;
		}
    }

	const UTempoAgentsSettings* TempoAgentsSettings = GetDefault<UTempoAgentsSettings>();
	if (TempoAgentsSettings == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - GetStraightestConnectionIndexForIntersection - Can't access TempoAgentsSettings.  Using hard-coded default settings."));
	}
	
	const float MaxThroughRoadAngleDegrees = TempoAgentsSettings != nullptr ? TempoAgentsSettings->GetMaxThroughRoadAngleDegrees() : 15.0f;
    const float MaxThroughRoadAngleRadians = FMath::DegreesToRadians(MaxThroughRoadAngleDegrees);

	if (StraightestRoadActorConnectionIndex == -1 || MaxDotProduct < FMath::Cos(MaxThroughRoadAngleRadians))
	{
		return false;
	}

	OutDestConnectionIndex = StraightestRoadActorConnectionIndex;
	OutConnectionDotProduct = MaxDotProduct;
    return true;
}

int32 UTempoIntersectionControlComponent::GetTrafficLightAnchorConnectionIndex(const AActor& IntersectionQueryActor, int32 SourceConnectionIndex, ETempoRoadConfigurationDescriptor& OutRoadConfigurationDescriptor) const
{
	const TArray<ETempoRoadConfigurationDescriptor>& PrioritizedRoadConfigurationDescriptors = {ETempoRoadConfigurationDescriptor::ThroughRoad, ETempoRoadConfigurationDescriptor::RightTurn, ETempoRoadConfigurationDescriptor::LeftTurn};

	const auto& PreferForwardTrafficLightAnchorRoad  = [](const FTempoRoadConfigurationInfo& RoadConfigurationInfo1, const FTempoRoadConfigurationInfo& RoadConfigurationInfo2)
	{
		return RoadConfigurationInfo1.ConnectionDotProduct > RoadConfigurationInfo2.ConnectionDotProduct;
	};
	
	const FTempoRoadConfigurationInfo& PrioritizedRoadConfigurationInfo = GetPrioritizedRoadConfigurationInfo(IntersectionQueryActor, SourceConnectionIndex, PrioritizedRoadConfigurationDescriptors, PreferForwardTrafficLightAnchorRoad);

	// If we couldn't get a valid RoadConfigurationInfo, ...
	if (!PrioritizedRoadConfigurationInfo.IsValid())
	{
		// Then, default to using the source connection as the anchor road.
		OutRoadConfigurationDescriptor = ETempoRoadConfigurationDescriptor::ThroughRoad;
		return SourceConnectionIndex;
	}

	// Return data from valid PrioritizedRoadConfigurationInfo.
	OutRoadConfigurationDescriptor = PrioritizedRoadConfigurationInfo.RoadConfigurationDescriptor;
	return PrioritizedRoadConfigurationInfo.DestConnectionIndex;
}

FTempoRoadConfigurationInfo UTempoIntersectionControlComponent::GetPrioritizedRoadConfigurationInfo(const AActor& IntersectionQueryActor, int32 SourceConnectionIndex, const TArray<ETempoRoadConfigurationDescriptor>& PrioritizedRoadConfigurationDescriptors, const TFunction<bool(const FTempoRoadConfigurationInfo&, const FTempoRoadConfigurationInfo&)>& SortPredicate) const
{
	for (const auto PrioritizedRoadConfigurationDescriptor : PrioritizedRoadConfigurationDescriptors)
	{
		TArray<FTempoRoadConfigurationInfo> RoadConfigurationInfos = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoRoadConfigurationInfo, SourceConnectionIndex, PrioritizedRoadConfigurationDescriptor);
		if (!RoadConfigurationInfos.IsEmpty())
		{
			RoadConfigurationInfos.Sort(SortPredicate);
			return RoadConfigurationInfos[0];
		}
	}

	return FTempoRoadConfigurationInfo();
}

float UTempoIntersectionControlComponent::GetLateralOffsetFromControlPoint(const AActor& RoadQueryActor, ETempoRoadOffsetOrigin LateralOffsetOrigin, float InLateralOffset) const
{
	const float RoadWidth = UTempoCoreUtils::CallBlueprintFunction(&RoadQueryActor, ITempoRoadInterface::Execute_GetTempoRoadWidth);
	const float LateralOriginSign = LateralOffsetOrigin == ETempoRoadOffsetOrigin::LeftRoadEdge ? -1.0f : 1.0f;
	const float AdjustedLateralOrigin = LateralOffsetOrigin == ETempoRoadOffsetOrigin::LeftRoadEdge || LateralOffsetOrigin == ETempoRoadOffsetOrigin::RightRoadEdge ? RoadWidth * 0.5f : 0.0f;

	const float LateralOffsetFromControlPoint = AdjustedLateralOrigin * LateralOriginSign + InLateralOffset;

	return LateralOffsetFromControlPoint;
}

void UTempoIntersectionControlComponent::SetupTrafficLightRuntimeData(AActor& OwnerActor, UMassTrafficControllerRegistrySubsystem& TrafficControllerRegistrySubsystem)
{
	const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(&OwnerActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);
	
	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		FTempoTrafficControllerMeshInfo TrafficControllerMeshInfo = UTempoCoreUtils::CallBlueprintFunction(&OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerMeshInfo, ConnectionIndex, ETempoTrafficControllerType::TrafficLight);
		const FVector TrafficControllerLocation = UTempoCoreUtils::CallBlueprintFunction(&OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerLocation, ConnectionIndex, ETempoTrafficControllerType::TrafficLight, ETempoCoordinateSpace::World);
		const FRotator TrafficControllerRotation = UTempoCoreUtils::CallBlueprintFunction(&OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerRotation, ConnectionIndex, ETempoTrafficControllerType::TrafficLight, ETempoCoordinateSpace::World);
		
		const FVector IntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(&OwnerActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, ConnectionIndex, ETempoCoordinateSpace::World);
		
		const AActor* RoadQueryActor = GetConnectedRoadActor(OwnerActor, ConnectionIndex);
		if (RoadQueryActor == nullptr)
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - SetupTrafficLightRuntimeData - Failed to get Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *OwnerActor.GetName(), ConnectionIndex);
			return;
		}

		constexpr int32 NumTrafficControllerMeshesPerTrafficLightType = 1;
		
		FStaticMeshInstanceVisualizationDesc StaticMeshInstanceVisualizationDesc;
		StaticMeshInstanceVisualizationDesc.Meshes.Reserve(NumTrafficControllerMeshesPerTrafficLightType);

		FMassStaticMeshInstanceVisualizationMeshDesc& MeshDesc = StaticMeshInstanceVisualizationDesc.Meshes.AddDefaulted_GetRef();
		MeshDesc.Mesh = TrafficControllerMeshInfo.TrafficControllerMesh;

		const int32 NumLanes = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetNumTempoLanes);
		
		FMassTrafficLightTypeData TrafficLightType(TrafficControllerMeshInfo.TrafficControllerMesh->GetFName(), StaticMeshInstanceVisualizationDesc, NumLanes);

		const int32 TrafficLightTypeIndex = TrafficControllerRegistrySubsystem.RegisterTrafficLightType(TrafficLightType);
		
		FMassTrafficLightInstanceDesc TrafficLightInstanceDesc(TrafficControllerLocation, TrafficControllerRotation.Yaw, IntersectionEntranceLocation, TrafficLightTypeIndex, TrafficControllerMeshInfo.MeshScale);
		
		TrafficControllerRegistrySubsystem.RegisterTrafficLight(TrafficLightInstanceDesc);
	}
}

void UTempoIntersectionControlComponent::SetupTrafficSignRuntimeData(AActor& OwnerActor, UMassTrafficControllerRegistrySubsystem& TrafficControllerRegistrySubsystem)
{
	const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(&OwnerActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);
	
	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		const FVector IntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(&OwnerActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, ConnectionIndex, ETempoCoordinateSpace::World);
		EMassTrafficControllerSignType TrafficControllerSignType = UTempoCoreUtils::CallBlueprintFunction(&OwnerActor, ITempoIntersectionInterface::Execute_GetTempoTrafficControllerSignType, ConnectionIndex);
		
		FMassTrafficSignInstanceDesc TrafficSignInstanceDesc(IntersectionEntranceLocation, TrafficControllerSignType);
		
		TrafficControllerRegistrySubsystem.RegisterTrafficSign(TrafficSignInstanceDesc);
	}
}

AActor* UTempoIntersectionControlComponent::GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const
{
	AActor* RoadActor = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor, ConnectionIndex);
			
	if (RoadActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - Failed to get Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex);
		return nullptr;
	}
			
	if (!RoadActor->Implements<UTempoRoadInterface>())
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("UTempoIntersectionControlComponent - Connected Road Actor for Actor: %s at ConnectionIndex: %d doesn't implement UTempoRoadInterface."), *IntersectionQueryActor.GetName(), ConnectionIndex);
		return nullptr;
	}

	return RoadActor;
}
