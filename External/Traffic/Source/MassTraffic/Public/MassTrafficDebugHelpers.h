// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "MassTraffic.h"

#include "Math/Color.h"
#include "MassEntityTypes.h"
#include "DrawDebugHelpers.h"
#include "MassEntityManager.h"


// Forward declarations
struct FMassTrafficLaneSegment;
struct FTransformFragment;
struct FZoneGraphStorage;
enum class EMassTrafficCombineDistanceToNextType : uint8;


namespace UE::MassTraffic
{


FString LogBugItGo(const FVector& Location, const FString& CommentString = FString(""), const float Z = 2000.0f, const bool bGo = false, const float SlomoIfGo = 1.0f, UWorld* World = nullptr);
FString LogBugItGo(const FTransform& Transform, const FString& CommentString = FString(""), const float Z = 2000.0f, const bool bGo = false, const float Slomo = 1.0f, UWorld* World = nullptr);
FString LogBugItGo(const FTransformFragment& TransformFragment, const FString& CommentString = FString(""), const float Z = 2000.0f, const bool bGo = false, const float Slomo = 1.0f, UWorld* World = nullptr);

FVector PointerToVector(const void* Ptr, float Size = 1.0f);
FColor PointerToColor(const void* Ptr);
FColor EntityToColor(const FMassEntityHandle Entity);

FVector GetPlayerViewLocation(const UWorld* World);

#if WITH_MASSTRAFFIC_DEBUG

void DrawDebugZLine(const UWorld* World, const FVector& Location, FColor Color = FColor::White, bool bPersist = false, float LifeTime = 0.0f, float Thickness= 10.0f, float Length = 750.0f);
void DrawDebugStringNearLocation(const UWorld* InWorld, FVector const& TextLocation, const FString& Text, class AActor* TestBaseActor, FColor const& TextColor, float Duration, bool bDrawShadow, float FontScale, const FVector Location = FVector::ZeroVector, const float Distance = 5000.0f);
void DrawDebugStringNearPlayerLocation(const UWorld* InWorld, FVector const& TextLocation, const FString& Text, class AActor* TestBaseActor = nullptr, FColor const& TextColor = FColor::White, float Duration = 0.0f, bool bDrawShadow = false, float FontScale = 1.0f);

void DrawDebugParkingSpace(UWorld* World, const FVector& Location, const FQuat& Rotation, const FColor& Color, bool bPersist = false, float Lifetime = 10.0f);
void DrawDebugTrafficLight(const UWorld* World, const FVector& Location, const FVector& XDirection, const FVector* IntersectionSideMidpoint = nullptr, const FColor ColorForVehicles = FColor::White, const FColor ColorForPedestrians_FrontSide = FColor::White, const FColor ColorForPedestrians_LeftSide = FColor::White, const FColor ColorForPedestrians_RightSide = FColor::White,bool bPersist = false, float Lifetime = 10.0f);
void DrawDebugSpeed(UWorld* World, const FVector& Location, const float Speed, const bool bBraking, const float DistanceAlongLane, const float CurrentLaneLength, const int32 LOD, const bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugChaosVehicleControl(UWorld* World, const FVector& Location, const FVector& SpeedControlChaseTargetLocation, const FVector& SteeringControlChaseTargetLocation, float TargetSpeed, float Throttle, float Brake, float Steering, bool bHandBrake, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugLaneSegment(UWorld* World, const FMassTrafficLaneSegment& LaneSegment, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugInterpolatedAxles(UWorld* World, const FVector& FrontAxleLocation, const FVector& RearAxleLocation, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugShouldStop(float DebugDrawSize, const FColor DebugDrawColor, const FString DebugText, bool bVisLog = false, const UObject* VisLogOwner = nullptr, const FTransform* VisLogTransform = nullptr);
void DrawDebugLaneChange(UWorld* World, const FTransform& Transform, bool bToLeftLane, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugLaneChangeProgression(UWorld* World, const FVector& Location, const FVector& Offset, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugDistanceToNext(UWorld* World, const FVector& VehicleLocation, const FVector& NextVehicleLocation, float DistanceToNext, EMassTrafficCombineDistanceToNextType CombineDistanceToNextDrawType, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugDensityManagementTransfer(UWorld* World, const FVector& TransferredFromLocation, const FVector& TransferredToLocation, const FColor& Color = FColor::Green, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugDensityManagementRecyclableVehicle(UWorld* World, const FVector& RecyclableVehicleLocation, bool bTransferred, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void DrawDebugSleepState(UWorld* World, const FVector& VehicleLocation, bool bIsSleeping, bool bVisLog = false, const UObject* VisLogOwner = nullptr);
void VisLogMalformedNextLaneLinks(const FMassEntityManager& EntityManager, int32 LaneIndex, const FMassEntityHandle TailVehicle, const FMassEntityHandle UndiscoveredVehicle, int32 MarchEjectAt, const UObject* VisLogOwner);

#else

inline void DrawDebugZLine(const UWorld* World, const FVector& Location, FColor Color = FColor::White, bool bPersist = false, float LifeTime = 0.0f, float Thickness = 10.0f, float Length = 750.0f) {}
inline void DrawDebugStringNearLocation(const UWorld* InWorld, FVector const& TextLocation, const FString& Text, class AActor* TestBaseActor, FColor const& TextColor, float Duration, bool bDrawShadow, float FontScale, const FVector Location = FVector::ZeroVector, const float Distance = 15000.0f) {}
inline void DrawDebugStringNearPlayerLocation(const UWorld* InWorld, FVector const& TextLocation, const FString& Text, class AActor* TestBaseActor = nullptr, FColor const& TextColor = FColor::White, float Duration = 0.0f, bool bDrawShadow = false, float FontScale = 1.0f) {}

inline void DrawDebugParkingSpace(UWorld* World, const FVector& Location, const FQuat& Rotation, uint8 VehicleTypeIndex, uint8 NumVehicleTypes, bool bPersist = false, float Lifetime = 10.0f) {}
inline void DrawDebugTrafficLight(const UWorld* World, const FVector& Location, const FVector& XDirection, const FVector* IntersectionSideMidpoint = nullptr, const FColor ColorForVehicles = FColor::White, const FColor ColorForPedestrians_FrontSide = FColor::White, const FColor ColorForPedestrians_LeftSide = FColor::White, const FColor ColorForPedestrians_RightSide = FColor::White,bool bPersist = false, float Lifetime = 10.0f) {}
inline void DrawDebugSpeed(UWorld* World, const FVector& Location, const float Speed, const bool bBraking, const float DistanceAlongLane, const float CurrentLaneLength, const int32 LOD, const bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugChaosVehicleControl(UWorld* World, const FVector& Location, const FVector& SpeedControlChaseTargetLocation, const FVector& SteeringControlChaseTargetLocation, float TargetSpeed, float Throttle, float Brake, float Steering, bool bHandBrake, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugLaneSegment(UWorld* World, const FMassTrafficLaneSegment& LaneSegment, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugInterpolatedAxles(UWorld* World, const FVector& FrontAxleLocation, const FVector& RearAxleLocation, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugShouldStop(float DebugDrawSize, const FColor DebugDrawColor, bool bVisLog = false, const UObject* VisLogOwner = nullptr, const FTransform* VisLogTransform = nullptr) {}
inline void DrawDebugLaneChange(UWorld* World, const FTransform& Transform, bool bToLeftLane, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugLaneChangeProgression(UWorld* World, const FVector& Location, const FVector& Offset, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugDistanceToNext(UWorld* World, const FVector& VehicleLocation, const FVector& NextVehicleLocation, float DistanceToNext, EMassTrafficCombineDistanceToNextType CombineDistanceToNextDrawType, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugDensityManagementTransfer(UWorld* World, const FVector& TransferredFromLocation, const FVector& TransferredToLocation, const FColor& Color = FColor::Green, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugDensityManagementRecyclableVehicle(UWorld* World, const FVector& RecyclableVehicleLocation, bool bTransferred, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void DrawDebugSleepState(UWorld* World, const FVector& VehicleLocation, bool bIsSleeping, bool bVisLog = false, const UObject* VisLogOwner = nullptr) {}
inline void VisLogMalformedNextLaneLinks(const FMassEntityManager& EntityManager, int32 LaneIndex, const FMassEntityHandle TailVehicle, const FMassEntityHandle UndiscoveredVehicle, int32 MarchEjectAt, const UObject* VisLogOwner) {}

#endif

}
