// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficDebugHelpers.h"

#include "MassEntityView.h"
#include "MassTrafficFragments.h"
#include "MassTrafficLaneChange.h"

#include "MassCommonFragments.h"
#include "MassDebugger.h"
#include "MassZoneGraphNavigationFragments.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "VisualLogger/VisualLogger.h"
#include "ZoneGraphTypes.h"
#include "ZoneGraphQuery.h"
#include "GameFramework/CheatManager.h"
#if WITH_EDITOR
#include "LevelEditorViewport.h"
#endif


namespace UE::MassTraffic
{

	
const float ViewerDistance = 10000.0f;

	
FString LogBugItGo(const FVector& Location, const FString& CommentString, const float Z, const bool bGo, const float SlomoIfGo, UWorld* World)
{
	const FVector ViewLocation = Location + FVector(0.5f * Z * Location.GetSafeNormal().X, 0.5f * Z * Location.GetSafeNormal().Y, Z);
	FRotator ViewRotation = UKismetMathLibrary::MakeRotFromX(Location - ViewLocation);
	ViewRotation.Roll = 0.0f;

	#if ENABLE_DRAW_DEBUG
	if (World)
	{
		DrawDebugZLine(World, Location, FColor::Red, false, 5.0f);	
	}
	#endif
	
	const FString BugItGoString = FString::Printf(TEXT("BugItGo %f %f %f %f %f %f"),
		ViewLocation.X, ViewLocation.Y, ViewLocation.Z,
		ViewRotation.Pitch, ViewRotation.Yaw, ViewRotation.Roll);
	
	if (!CommentString.IsEmpty())
	{
		UE_LOG(LogMassTraffic, Display, TEXT("    %s    # %s"), *BugItGoString, *CommentString);	
	}
	else
	{
		UE_LOG(LogMassTraffic, Display, TEXT("    %s"), *BugItGoString);
	}

	if (bGo)
	{
		// @see UEditorEngine::HandleBugItGoCommand
#if WITH_EDITOR
		if (GEditor && !GEditor->PlayWorld)
		{
			for(FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
			{
				ViewportClient->SetViewLocation(ViewLocation);
				ViewportClient->SetViewRotation(ViewRotation);
			}
			GEditor->RedrawLevelEditingViewports();
		}
		else
		{		
#endif
			// Get first local PlayerController
            for (FConstPlayerControllerIterator Iterator = GWorld->GetPlayerControllerIterator(); Iterator; ++Iterator)
            {
            	APlayerController* PlayerController = Iterator->Get();
            	if (PlayerController && PlayerController->IsLocalController())
            	{
            		if (PlayerController->CheatManager)
            		{
            			PlayerController->CheatManager->BugItWorker(ViewLocation, ViewRotation);
	            		PlayerController->CheatManager->Slomo(SlomoIfGo);
            		}
            		break;
            	}
            }
#if WITH_EDITOR
		}
#endif
	}

	return BugItGoString;
}

	
FString LogBugItGo(const FTransform& Transform, const FString& CommentString, const float Z, const bool bGo, const float Slomo, UWorld* World)
{
	return LogBugItGo(Transform.GetLocation(), CommentString, Z, bGo, Slomo, World);
}

	
FString LogBugItGo(const FTransformFragment& TransformFragment, const FString& CommentString, const float Z, const bool bGo, const float Slomo, UWorld* World)
{
	return LogBugItGo(TransformFragment.GetTransform(), CommentString, Z, bGo, Slomo, World);
}
	
	
FVector PointerToVector(const void* Ptr, float Size)
{
	int32 Seed = 0;
	{
		int64 Mask = 0x1;
		for (uint8 I = 0; I < 64; I++)
		{
			Seed += int64(Ptr) & Mask;
			Mask *= 2;
		}
	}
	FRandomStream RandomStream(Seed);
	return RandomStream.VRand() * Size;
}

FColor PointerToColor(const void* Ptr)
{
	int32 Seed = 0;
	{
		int64 Mask = 0x1;
		for (uint8 I = 0; I < 64; I++)
		{
			Seed += int64(Ptr) & Mask;
			Mask *= 2;
		}
	}

	const uint8 Hue = static_cast<uint8>(FRandomStream(Seed).FRand()*255.f);
	return FLinearColor::MakeFromHSV8(Hue, 255, 255).ToFColor(/*bSRGB*/true);
}

FColor EntityToColor(const FMassEntityHandle Entity)
{
	const uint8 Hue = static_cast<uint8>(FRandomStream(Entity.SerialNumber).FRand() * 255.f);
	return FLinearColor::MakeFromHSV8(Hue, 255, 255).ToFColor(/*bSRGB*/true);
}

FVector GetPlayerViewLocation(const UWorld* World)
{
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(World, 0);
	return CameraManager != nullptr ? CameraManager->GetCameraLocation() : FVector::ZeroVector;
}

	
#if WITH_MASSTRAFFIC_DEBUG
	
void DrawDebugZLine(const UWorld* World, const FVector& Location, FColor Color, bool bPersist, float LifeTime, float Thickness, float Length)
{
	const FVector Z(0.0f, 0.0f, Length);
	DrawDebugLine(World, Location, Location + Z, Color, bPersist, LifeTime, 0, Thickness);
}
	
void DrawDebugStringNearLocation(const UWorld* InWorld, FVector const& TextLocation, const FString& Text, class AActor* TestBaseActor, FColor const& TextColor, float Duration, bool bDrawShadow, float FontScale, const FVector Location, const float Distance)
{
	if (FVector::Distance(TextLocation, Location) <= Distance)
	{
		DrawDebugString(InWorld, TextLocation, Text, TestBaseActor, TextColor, Duration, bDrawShadow, FontScale);
	}
}

void DrawDebugStringNearPlayerLocation(const UWorld* InWorld, FVector const& TextLocation, const FString& Text, class AActor* TestBaseActor, FColor const& TextColor, float Duration, bool bDrawShadow, float FontScale)
{
	DrawDebugStringNearLocation(InWorld, TextLocation, Text, TestBaseActor, TextColor, Duration, bDrawShadow, FontScale, GetPlayerViewLocation(InWorld), ViewerDistance);
}

void DrawDebugParkingSpace(UWorld* World, const FVector& Location, const FQuat& Rotation, const FColor& Color, bool bPersist, float Lifetime)
{
	// Parked car location
	DrawDebugPoint(World, Location, 20.0f, Color, bPersist, Lifetime);
	
	// Parked car rotation
	DrawDebugLine(World, Location, Location + Rotation.GetForwardVector() * 100.0f, Color, bPersist, Lifetime, 0, 5.0f);
}
	
void DrawDebugTrafficLight(const UWorld* World, const FVector& Location, const FVector& XDirection, const FVector* IntersectionSideMidpoint,
	const FColor ColorForVehicles, const FColor ColorForPedestrians_FrontSide, const FColor ColorForPedestrians_LeftSide, const FColor ColorForPedestrians_RightSide,
	bool bPersist, float Lifetime)
{
	const float Thickness = 20.0f;
	const float ArrowSize = 50.0f;
	const float ArrowLength = 200.0f;
	const FVector Z(0.0f, 0.0f, 200.0f);
	const FRotator RotLeft(0.0f, -90.0f, 0.0f);
	const FRotator RotRight(0.0f, 90.0f, 0.0f);
	
	// Traffic light post.
	DrawDebugLine(World, Location, Location + Z,
		ColorForVehicles, bPersist, Lifetime, 0, Thickness);
	// Traffic light direction.
	DrawDebugDirectionalArrow(World, Location + Z, Location + Z + ArrowLength * XDirection, ArrowSize,
		ColorForVehicles, bPersist, Lifetime, 0, Thickness);

	// Pedestrian light - front side of light.
	DrawDebugDirectionalArrow(World, Location + Z/2.0f, Location + Z/2.0f + ArrowLength/2.0f * XDirection, ArrowSize,
		ColorForPedestrians_FrontSide, bPersist, Lifetime, 0, Thickness/2.0f);
	// Pedestrian light - left side of light.
	DrawDebugDirectionalArrow(World, Location + Z/2.0f, Location + Z/2.0f + ArrowLength/2.0f * RotLeft.RotateVector(XDirection), ArrowSize/2.0f,
		ColorForPedestrians_LeftSide, bPersist, Lifetime, 0, Thickness/2.0f);
	// Pedestrian light - right side of light.
	DrawDebugDirectionalArrow(World, Location + Z/2.0f, Location + Z/2.0f + ArrowLength/2.0f * RotRight.RotateVector(XDirection), ArrowSize/2.0f,
		ColorForPedestrians_RightSide, bPersist, Lifetime, 0, Thickness/2.0f);

	// So traffic light to controlled intersection side connection.
	if (IntersectionSideMidpoint)
	{
		// Middle of intersection side post.
		DrawDebugLine(World, *IntersectionSideMidpoint,  *IntersectionSideMidpoint + Z, FColor::Blue, bPersist, Lifetime, 0, Thickness);
		
		// Line connecting traffic light point to middle of intersection side post.
		DrawDebugDirectionalArrow(World, *IntersectionSideMidpoint + Z, Location + Z, 25.0f, FColor::Purple, bPersist, Lifetime, 0, Thickness/2.0f);
	}
}

void DrawDebugSpeed(UWorld* World, const FVector& Location, const float Speed, const bool bBraking, const float DistanceAlongLane, const float CurrentLaneLength, const int32 LOD, const bool bVisLog, const UObject* VisLogOwner)
{
#if ENABLE_DRAW_DEBUG || ENABLE_VISUAL_LOG 
	
	constexpr float Size = 100.0f; 
	constexpr float Thickness = 20.0f; 
	constexpr float NormalizationSpeedMPH = 70.0f;
	constexpr float NormalizationSpeed = NormalizationSpeedMPH * 100000.f / 2236.94185f;
	
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugSpeed && FVector::Distance(GetPlayerViewLocation(VisLogOwner->GetWorld()), Location) <= ViewerDistance)
	{
		const FVector LineOffset(0.0f,0.0f,300.0f);
		const FVector TextOffset(0.0f,0.0f,400.0f);

		const FString Str = FString::Printf(TEXT("SPD:%.1f BRK?%d D%%:%.3f LOD:%d"), Speed, bBraking, (DistanceAlongLane/CurrentLaneLength));
		DrawDebugString(World, Location + TextOffset, Str, nullptr, FColor::White, 0.0f);
	
		// Base line
		DrawDebugLine(World, Location + LineOffset, Location + LineOffset + FVector(0.0f, 0.0f, Size), FColor::Black, false, -1, 0, Thickness);

		// Speed percentage line
		const float SpeedLineSize = Speed / NormalizationSpeed * Size;
		DrawDebugLine(World, Location + LineOffset, Location + LineOffset + FVector(0.0f, 0.0f, SpeedLineSize), bBraking ? FColor::Red : FColor::Green, false, -1, 1, Thickness);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (bVisLog)
	{
		UE_VLOG_LOCATION(VisLogOwner, LogMassTraffic, Log, Location, 5.0f, bBraking ? FColor::Red : FColor::Green, TEXT("Speed: %0.2f\nDistance: %0.2f / %0.2f"), Speed, DistanceAlongLane, CurrentLaneLength);
	}
#endif

#endif
}

void DrawDebugChaosVehicleControl(UWorld* World, const FVector& Location, const FVector& SpeedControlChaseTargetLocation, const FVector& SteeringControlChaseTargetLocation , float TargetSpeed, float Throttle, float Brake, float Steering, bool bHandBrake, bool bVisLog, const UObject* VisLogOwner)
{
	// Debug / VisLogging
    #if ENABLE_VISUAL_LOG
    	if (GMassTrafficDebugSpeed)
    	{
    		DrawDebugPoint(World, SpeedControlChaseTargetLocation, /*Size*/10.0f, FColor::Green);
    		DrawDebugLine(World, Location + FVector(0,0,100), SpeedControlChaseTargetLocation, FColor::Green);
    		
    		DrawDebugPoint(World, SteeringControlChaseTargetLocation, /*Size*/10.0f, FColor::Turquoise);
    		DrawDebugLine(World, Location + FVector(0,0,100), SteeringControlChaseTargetLocation, FColor::Turquoise);

    		const FVector TextOffset(0.0f,0.0f,450.0f);
			const FString Str = FString::Printf(TEXT("TARG:%.1f THR:%.1f STR:%.2f"), TargetSpeed, Throttle, Steering);
			DrawDebugString(World, Location + TextOffset, Str, nullptr, FColor::White, 0.0f);
    	}
    	if (bVisLog)
    	{
    		UE_VLOG_SEGMENT(VisLogOwner, TEXT("MassTraffic Physics"), Display, Location + FVector(0,0,100), SpeedControlChaseTargetLocation, bHandBrake ? FColor::Red : FColor::Green, TEXT("TargetSpeed: %0.2f\nThrottle: %f\nBrake: %f"), TargetSpeed, Throttle, Brake);
    		UE_VLOG_SEGMENT(VisLogOwner, TEXT("MassTraffic Physics"), Display, Location + FVector(0,0,100), SteeringControlChaseTargetLocation, FColor::Turquoise, TEXT("Steering: %f"), Steering);
    	}
    #endif
}

void DrawDebugLaneSegment(UWorld* World, const FMassTrafficLaneSegment& LaneSegment, bool bVisLog, const UObject* VisLogOwner)
{
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugInterpolation)
	{
		DrawDebugPoint(World, LaneSegment.StartPoint, 20.0f, FColor::Red);
		DrawDebugPoint(World, LaneSegment.StartControlPoint, 20.0f, FColor::Green);
		DrawDebugPoint(World, LaneSegment.EndControlPoint, 20.0f, FColor::Blue);
		DrawDebugPoint(World, LaneSegment.EndPoint, 20.0f, FColor::Cyan);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (bVisLog)
	{
		UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Interpolation"), Log, LaneSegment.StartPoint, 20.0f, FColor::Red, TEXT(""));
		UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Interpolation"), Log, LaneSegment.StartControlPoint, 20.0f, FColor::Green, TEXT(""));
		UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Interpolation"), Log, LaneSegment.EndControlPoint, 20.0f, FColor::Blue, TEXT(""));
		UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Interpolation"), Log, LaneSegment.EndPoint, 20.0f, FColor::Cyan, TEXT(""));
	}
#endif
}

void DrawDebugInterpolatedAxles(UWorld* World, const FVector& FrontAxleLocation, const FVector& RearAxleLocation, bool bVisLog, const UObject* VisLogOwner)
{
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugInterpolation)
	{
		DrawDebugPoint(World, FrontAxleLocation, 20.0f, FColor::White);
		DrawDebugPoint(World, RearAxleLocation, 20.0f, FColor::Black);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (bVisLog || GMassTrafficDebugInterpolation >= 2)
	{
		UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Interpolation"), Log, FrontAxleLocation, 20.0f, FColor::White, TEXT(""));
		UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Interpolation"), Log, RearAxleLocation, 20.0f, FColor::Black, TEXT(""));
	}
#endif
}


void DrawDebugShouldStop(float DebugDrawSize, const FColor DebugDrawColor, const FString DebugText, bool bVisLog, const UObject* VisLogOwner, const FTransform* VisLogTransform)
{
#if ENABLE_DRAW_DEBUG || ENABLE_VISUAL_LOG 

	if (!VisLogOwner || !VisLogTransform)
	{
		return;
	}

	const FVector DebugDrawDotLocation = VisLogTransform->GetLocation() + FVector(0.0, 0.0, 300.0);
	const FVector DebugDrawTextLocation = VisLogTransform->GetLocation() + FVector(0.0, 0.0, 400.0);
	
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugShouldStop && FVector::Distance(GetPlayerViewLocation(VisLogOwner->GetWorld()), VisLogTransform->GetLocation()) <= ViewerDistance)
	{
		DrawDebugPoint(VisLogOwner->GetWorld(), DebugDrawDotLocation, DebugDrawSize, DebugDrawColor);
		DrawDebugString(VisLogOwner->GetWorld(), DebugDrawTextLocation, DebugText, nullptr, FColor::White, 0.0f, true, 1.0f);
	}
#endif
	
#if ENABLE_VISUAL_LOG
	if (bVisLog || GMassTrafficDebugShouldStop >= 2)
	{
		UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic"), Log, DebugDrawDotLocation, DebugDrawSize, DebugDrawColor, TEXT("%s"), *DebugText);
	}
#endif

#endif
}

	
void DrawDebugLaneChange(UWorld* World, const FTransform& Transform, bool bToLeftLane, bool bVisLog, const UObject* VisLogOwner)
{
#if ENABLE_DRAW_DEBUG || ENABLE_VISUAL_LOG 

	const float Thickness = 5.0f;
	const FColor Color = FColor::Green;

	const FVector LineStart = Transform.GetLocation();
	const FVector LineEnd = Transform.TransformPosition(FVector::RightVector * (bToLeftLane ? -100.0f : 100.0f));
	
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugLaneChanging && World)
	{
		DrawDebugLine(World, LineStart, LineEnd, Color, false, 0.0f, 0, Thickness);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (VisLogOwner && (bVisLog || GMassTrafficDebugLaneChanging >= 2))
	{
		UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic LaneChange"), Display, LineStart, LineEnd, Color, Thickness, TEXT("Lane Change"));
	}
#endif

	#endif
}
	
void DrawDebugLaneChangeProgression(UWorld* World, const FVector& Location, const FVector& Offset, bool bVisLog, const UObject* VisLogOwner)
{
	const FVector Z(0.0f, 0.0f, 600.0f);
	const float Thickness = 40.0f;
	
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugLaneChanging && World)
	{
		DrawDebugLine(World, Location, Location + Z, FColor::Emerald, false, 0.0f, 0, Thickness);
		DrawDebugLine(World, Location, Location - Offset, FColor::White, false, 0.0f, 0, Thickness);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (VisLogOwner && (bVisLog || GMassTrafficDebugLaneChanging >= 2))
	{
		UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic LaneChange"), Verbose, Location, Location + Z, FColor::Emerald, Thickness, TEXT(""));
		UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic LaneChange"), Verbose, Location, Location - Offset, FColor::White, Thickness, TEXT("Lane Change"));
	}
#endif
}

	
void DrawDebugDistanceToNext(UWorld* World, const FVector& VehicleLocation, const FVector& NextVehicleLocation, float DistanceToNext, EMassTrafficCombineDistanceToNextType CombineDistanceToNextDrawType, bool bVisLog, const UObject* VisLogOwner)
{
	if (GMassTrafficDebugDistanceToNext < 3 && FVector::Distance(VehicleLocation, GetPlayerViewLocation(World)) > 20000.0f)
	{
		return;
	}


	FColor Color = FColor::White;
	if (CombineDistanceToNextDrawType == EMassTrafficCombineDistanceToNextType::Next)
	{
		Color = FColor::Magenta;	
	}
	if (CombineDistanceToNextDrawType == EMassTrafficCombineDistanceToNextType::LaneChangeNext)
	{
		Color = FColor::Emerald;	
	}
	if (CombineDistanceToNextDrawType == EMassTrafficCombineDistanceToNextType::SpittingLaneGhostNext)
	{
		Color = FColor::Blue;	
	}
	if (CombineDistanceToNextDrawType == EMassTrafficCombineDistanceToNextType::MergingLaneGhostNext)
	{
		Color = FColor::Turquoise;	
	}
	
	// Using color as a cheat to make a differentiating offset .. so different 'types' (colors) of lines don't completely
	// overlap each other, making some of them impossible to see.
	const float OffsetX = (static_cast<float>(Color.R) / 255.0f - 0.5f) * 50.0f;
	const float OffsetY = (static_cast<float>(Color.G) / 255.0f - 0.5f) * 50.0f;
	const float OffsetZ = (static_cast<float>(Color.B) / 255.0f - 0.5f) * 50.0f;
	
	const FVector OffsetLow(OffsetX, OffsetY, OffsetZ);
	const FVector OffsetHigh = OffsetLow + FVector(0.0f, 0.0f, 500.0f);

	/*
	const float Alpha = FMath::Clamp(FVector::Distance(VehicleLocation, NextVehicleLocation) / 5000.0f, 0.0f, 1.0f);
	const float Thickness = FMath::Lerp(30.0f, 10.0f, Alpha);
	*/
	const float Thickness = 15.0f;

#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugDistanceToNext == 1 ||
		GMassTrafficDebugDistanceToNext == 2 ||
		(GMassTrafficDebugDistanceToNext == 11 && CombineDistanceToNextDrawType != EMassTrafficCombineDistanceToNextType::Next) ||
		(GMassTrafficDebugDistanceToNext == 12 && CombineDistanceToNextDrawType != EMassTrafficCombineDistanceToNextType::Next))
	{
		DrawDebugLine(World, VehicleLocation + OffsetLow, VehicleLocation + OffsetHigh, FColor::Silver, false, -1.0f, 0, Thickness);
		DrawDebugLine(World, VehicleLocation + OffsetHigh, NextVehicleLocation /*+ OffsetHigh*/, Color, false, -1.0f, 0, Thickness);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (GMassTrafficDebugDistanceToNext >= 2 ||
		(GMassTrafficDebugDistanceToNext == 12 && CombineDistanceToNextDrawType != EMassTrafficCombineDistanceToNextType::Next))
	{
		UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic NextVehicle"), Display, VehicleLocation + OffsetLow, VehicleLocation + OffsetHigh, Color, Thickness, TEXT(""));
		UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic NextVehicle"), Display, VehicleLocation + OffsetHigh, NextVehicleLocation, Color, Thickness, TEXT("%0.2f"), DistanceToNext);
	}
#endif	
}

void DrawDebugDensityManagementTransfer(UWorld* World, const FVector& TransferredFromLocation, const FVector& TransferredToLocation, const FColor& Color, bool bVisLog, const UObject* VisLogOwner)
{
	const float Thickness = 100.0f;
	const FVector Offset(0,0,500);
	
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugOverseer)
	{
		DrawDebugDirectionalArrow(World, TransferredFromLocation + Offset, TransferredToLocation + Offset, Thickness * 5.0f, Color, false, 0.5f, 0, Thickness);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (bVisLog || GMassTrafficDebugOverseer >= 2)
	{
		UE_VLOG_ARROW(VisLogOwner, TEXT("MassTraffic DensityManagement"), Display, TransferredFromLocation + Offset, TransferredToLocation + Offset, Color, TEXT(""));
	}
#endif
}

void DrawDebugDensityManagementRecyclableVehicle(UWorld* World, const FVector& RecyclableVehicleLocation, bool bTransferred, bool bVisLog, const UObject* VisLogOwner)
{
	const FVector Offset(0,0,400);
	
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugOverseer)
	{
		DrawDebugPoint(World, RecyclableVehicleLocation + Offset, 10.0f, bTransferred ? FColor::Green : FColor::Red);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (bVisLog || GMassTrafficDebugOverseer >= 2)
	{
		if (bTransferred)
		{
			UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic DensityManagement"), Display, RecyclableVehicleLocation, 50.0f, FColor::Green, TEXT(""));
		}
		else
		{
			UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic DensityManagement"), Warning, RecyclableVehicleLocation, 50.0f, FColor::Red, TEXT("Failed to transfer recyclable vehicle"));
		}
	}
#endif
}

void DrawDebugSleepState(UWorld* World, const FVector& VehicleLocation, bool bIsSleeping, bool bVisLog, const UObject* VisLogOwner)
{
	const FVector Offset(100,0,400);
	
#if ENABLE_DRAW_DEBUG
	if (GMassTrafficDebugSleep)
	{
		DrawDebugPoint(World, VehicleLocation + Offset, 10.0f, bIsSleeping ? FColor::Red : FColor::White);
	}
#endif

#if ENABLE_VISUAL_LOG
	if (bVisLog || GMassTrafficDebugSleep >= 2)
	{
		if (bIsSleeping)
		{
			UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Sleep"), Display, VehicleLocation, 50.0f, FColor::Red, TEXT("Zzzz"));
		}
		else
		{
			UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Sleep"), Warning, VehicleLocation, 50.0f, FColor::White, TEXT(""));
		}
	}
#endif
}

void VisLogMalformedNextLaneLinks(const FMassEntityManager& EntityManager, int32 LaneIndex, const FMassEntityHandle TailVehicle, const FMassEntityHandle UndiscoveredVehicle, int32 MarchEjectAt, const UObject* VisLogOwner)
{
#if ENABLE_VISUAL_LOG
	
	constexpr float ZHeight = 500.0f;
	
	if (!VisLogOwner)
	{
		return;
	}
	
	check(TailVehicle.IsSet());
	
	const FMassEntityView TailVehicleEntityView(EntityManager, TailVehicle);
	const FVector MalformedLaneMessageLocation = TailVehicleEntityView.GetFragmentData<FTransformFragment>().GetTransform().GetLocation() + FVector(0,0,1000);
	UE_VLOG_LOCATION(VisLogOwner, TEXT("MassTraffic Validation"), Error, MalformedLaneMessageLocation, 50.0f, FColor::Red, TEXT("Lane %d's NextVehicle links are malformed"), LaneIndex);

	if (UndiscoveredVehicle.IsSet())
	{
		const FMassEntityView UndiscoveredVehicleEntityView(EntityManager, UndiscoveredVehicle);
		const FVector UndiscoveredVehicleLocation = UndiscoveredVehicleEntityView.GetFragmentData<FTransformFragment>().GetTransform().GetLocation();
		const FMassZoneGraphLaneLocationFragment& UndiscoveredVehicleLaneLocationFragment = UndiscoveredVehicleEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic Validation"), Error, MalformedLaneMessageLocation, UndiscoveredVehicleLocation, FColor::Red, 2.0f, TEXT("Never encountered %d while marching along lane %d @ %0.2f"), UndiscoveredVehicle.Index, UndiscoveredVehicleLaneLocationFragment.LaneHandle.Index, UndiscoveredVehicleLaneLocationFragment.DistanceAlongLane);
	}

	int32 LoopCount = 0;

	TSet<FMassEntityHandle> VisitedEntities;

	FMassEntityView MarchingVehicleEntityView = TailVehicleEntityView;
	while (MarchingVehicleEntityView.IsSet())
	{
		FMassZoneGraphLaneLocationFragment& LaneLocationFragment = MarchingVehicleEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		FTransformFragment& TransformFragment = MarchingVehicleEntityView.GetFragmentData<FTransformFragment>();
		FMassTrafficNextVehicleFragment& NextVehicleFragment = MarchingVehicleEntityView.GetFragmentData<FMassTrafficNextVehicleFragment>();

		// Pointing to self?
		if (NextVehicleFragment.GetNextVehicle() == MarchingVehicleEntityView.GetEntity())
		{
			UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic Validation"), Error, TransformFragment.GetTransform().GetLocation(), TransformFragment.GetTransform().GetLocation() + FVector(0,0,ZHeight), FColor::Red, 5.0f, TEXT("%d's NextVehicle is pointing to itself on lane %d"), MarchingVehicleEntityView.GetEntity().Index, LaneLocationFragment.LaneHandle.Index);
			break;
		}

		// Infinite loop check
		if (VisitedEntities.Contains(MarchingVehicleEntityView.GetEntity()))
		{
			// Log march eject 
			UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic Validation"), Error, TransformFragment.GetTransform().GetLocation(), TransformFragment.GetTransform().GetLocation() + FVector(0,0,ZHeight), FColor::Red, 5.0f, TEXT("Ifinitie loop detected after revisiting %d on lane %d"), MarchingVehicleEntityView.GetEntity().Index, LaneLocationFragment.LaneHandle.Index);
			
			break;
		}
		else
		{
			VisitedEntities.Add(MarchingVehicleEntityView.GetEntity());
		}

		// March eject?
		if (LoopCount >= MarchEjectAt)
		{
			// Log march eject 
			UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic Validation"), Error, TransformFragment.GetTransform().GetLocation(), TransformFragment.GetTransform().GetLocation() + FVector(0,0,ZHeight), FColor::Red, 5.0f, TEXT("%d on lane %d - march eject at %d"), MarchingVehicleEntityView.GetEntity().Index, LaneLocationFragment.LaneHandle.Index, LoopCount);
			
			break;
		}
		
		// Log vehicle on lane 
		const uint8 Hue = static_cast<uint8>(FRandomStream(LaneLocationFragment.LaneHandle.Index).FRand()*255.f);
		const FColor LaneColor = FLinearColor::MakeFromHSV8(Hue, 255, 255).ToFColor(/*bSRGB*/true);
		UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic Validation"), Display, TransformFragment.GetTransform().GetLocation(), TransformFragment.GetTransform().GetLocation() + FVector(0,0,ZHeight), LaneColor, 5.0f, TEXT("%d on lane %d"), MarchingVehicleEntityView.GetEntity().Index, LaneLocationFragment.LaneHandle.Index);

		// Keep going?
		FMassEntityHandle NextVehicle = NextVehicleFragment.GetNextVehicle();
		if (!NextVehicle.IsSet())
		{
			// No more links to traverse
			break;
		}

		
		// Log line to next
		FMassEntityView NextVehicleEntityView(EntityManager, NextVehicle);
		FTransformFragment& NextVehicleTransformFragment = NextVehicleEntityView.GetFragmentData<FTransformFragment>();

		// Looped back to tail?
		if (NextVehicle == TailVehicle)
		{
			UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic Validation"), Warning, TransformFragment.GetTransform().GetLocation() + FVector(0,0,ZHeight), NextVehicleTransformFragment.GetTransform().GetLocation(), FColor::Orange, 5.0f, TEXT("%d loops back to the tail %d"), MarchingVehicleEntityView.GetEntity().Index, TailVehicle.Index);
			break;
		}
		else
		{
			UE_VLOG_SEGMENT_THICK(VisLogOwner, TEXT("MassTraffic Validation"), Warning, TransformFragment.GetTransform().GetLocation() + FVector(0,0,ZHeight), NextVehicleTransformFragment.GetTransform().GetLocation(), LaneColor, 5.0f, TEXT("%d -> %d"), MarchingVehicleEntityView.GetEntity().Index, NextVehicle.Index);
		}

		// Advance to next
		MarchingVehicleEntityView = NextVehicleEntityView;

		++LoopCount;
	}
#endif
}

void DrawDebugMassTrafficLaneData(const UMassTrafficSubsystem& MassTrafficSubsystem, const FMassEntityHandle& EntityHandle, const FMassTrafficVehicleControlFragment& VehicleControlFragment, const FMassZoneGraphLaneLocationFragment& LaneLocationFragment, const float LifeTime)
{
	if (GMassTrafficDebugLaneData == 0)	// 0 = Off (default)
	{
		return;
	}
	
	// Only draw debug lines for selected Entities.
	// (See console commands "mass.debug.DebugEntity" and "mass.debug.SetDebugEntityRange".)
	if (!UE::Mass::Debug::IsDebuggingEntity(EntityHandle))
	{
		return;
	}

	const FZoneGraphLaneHandle& CurrentLaneHandle = LaneLocationFragment.LaneHandle;
	
	const FMassTrafficZoneGraphData* ZoneGraphData = MassTrafficSubsystem.GetTrafficZoneGraphData(CurrentLaneHandle.DataHandle);
	const FZoneGraphTrafficLaneData* PrevLaneData = ZoneGraphData != nullptr && VehicleControlFragment.PreviousLaneIndex != INDEX_NONE ? ZoneGraphData->GetTrafficLaneData(VehicleControlFragment.PreviousLaneIndex) : nullptr;
	const FZoneGraphLaneHandle* PrevLaneHandle = PrevLaneData != nullptr ? &PrevLaneData->LaneHandle : nullptr;

	const FZoneGraphTrafficLaneData* NextLaneData = VehicleControlFragment.NextLane;
	const FZoneGraphLaneHandle* NextLaneHandle = NextLaneData != nullptr ? &NextLaneData->LaneHandle : nullptr;

	const FZoneGraphLaneHandle* ModifiedCurrentLaneHandle = &CurrentLaneHandle;
	const FZoneGraphLaneHandle* ModifiedPrevLaneHandle = PrevLaneHandle;
	const FZoneGraphLaneHandle* ModifiedNextLaneHandle = NextLaneHandle;

	// Apply longitudinal offset.
	if (GMassTrafficDebugLaneDataLongitudinalOffset < 0)
	{
		if (PrevLaneHandle != nullptr)
		{
			ModifiedCurrentLaneHandle = PrevLaneHandle;
			ModifiedPrevLaneHandle = nullptr;
			ModifiedNextLaneHandle = &CurrentLaneHandle;
		}
	}
	else if (GMassTrafficDebugLaneDataLongitudinalOffset > 0)
	{
		if (NextLaneHandle != nullptr)
		{
			ModifiedCurrentLaneHandle = NextLaneHandle;
			ModifiedPrevLaneHandle = &CurrentLaneHandle;
			ModifiedNextLaneHandle = nullptr;
		}
	}
		
	if (ensureMsgf(ModifiedCurrentLaneHandle != nullptr, TEXT("ModifiedCurrentLaneHandle should always be valid.  But, it is not.")))
	{
		DrawDebugMassTrafficLaneData(MassTrafficSubsystem, *ModifiedCurrentLaneHandle, ModifiedPrevLaneHandle, ModifiedNextLaneHandle, LifeTime);
	}
}

void DrawDebugMassTrafficLaneData(const UMassTrafficSubsystem& MassTrafficSubsystem, const FZoneGraphLaneHandle& CurrentLaneHandle, const FZoneGraphLaneHandle* PrevLaneHandle, const FZoneGraphLaneHandle* NextLaneHandle, const float LifeTime)
{
	if (GMassTrafficDebugLaneData == 0)	// 0 = Off (default)
	{
		return;
	}

	// We at least need the current lane data in order to draw any lines.
	const FZoneGraphTrafficLaneData* CurrentLaneData = MassTrafficSubsystem.GetTrafficLaneData(CurrentLaneHandle);
	if (CurrentLaneData == nullptr)
	{
		return;
	}

	// But, the previous and next lanes are optional.
	const FZoneGraphTrafficLaneData* PrevLaneData = PrevLaneHandle != nullptr ? MassTrafficSubsystem.GetTrafficLaneData(*PrevLaneHandle) : nullptr;
	const FZoneGraphTrafficLaneData* NextLaneData = NextLaneHandle != nullptr ? MassTrafficSubsystem.GetTrafficLaneData(*NextLaneHandle) : nullptr;
	
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(MassTrafficSubsystem.GetWorld());
	if (ZoneGraphSubsystem == nullptr)
	{
		return;
	}

	const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(CurrentLaneData->LaneHandle.DataHandle);
	if (ZoneGraphStorage == nullptr)
	{
		return;
	}
	
	const auto& DrawLaneData = [ZoneGraphSubsystem, ZoneGraphStorage, LifeTime](const FZoneGraphTrafficLaneData& LaneData, const FColor LaneColor, const float ZOffset = 10.0f)
	{
		if (!LaneData.LaneHandle.IsValid())
		{
			return;
		}
		
		const FZoneLaneData& ZoneLaneData = ZoneGraphStorage->Lanes[LaneData.LaneHandle.Index];
		const FVector& LaneStart = ZoneGraphStorage->LanePoints[ZoneLaneData.PointsBegin] + FVector(0.0f, 0.0f, ZOffset);
		const FVector& LaneEnd = ZoneGraphStorage->LanePoints[ZoneLaneData.PointsEnd - 1] + FVector(0.0f, 0.0f, ZOffset);
		
		const FVector LaneDir = (LaneEnd - LaneStart).GetSafeNormal();
		
		DrawDebugDirectionalArrow(ZoneGraphSubsystem->GetWorld(), LaneStart, LaneEnd, 100000.0f, LaneColor, false, LifeTime, 0, 10.0f);

		if (GMassTrafficDebugLaneData >= 2)	// 2 = Same as option 1, plus spheres to indicate turn state and HasVehiclesReadyToUseIntersectionLane()
		{
			FColor LaneTurnStateColor = FColor::Green;

			if (LaneData.bTurnsLeft)
			{
				LaneTurnStateColor = FColor::Blue;
			}
			else if (LaneData.bTurnsRight)
			{
				LaneTurnStateColor = FColor::Red;
			}

			DrawDebugSphere(ZoneGraphSubsystem->GetWorld(), LaneEnd + FVector(0.0f, 0.0f, ZOffset), 100.0f, 32, LaneTurnStateColor, false, LifeTime, 0, 10.0f);

			if (LaneData.HasVehiclesReadyToUseIntersectionLane())	// (See all READYLANE.)
			{
				constexpr float ReadyToUseLaneSphereOffset = 10.0f;
				const FVector ReadyToUseLaneSphereLocation = LaneEnd - LaneDir * ReadyToUseLaneSphereOffset + FVector(0.0f, 0.0f, ZOffset);
				DrawDebugSphere(ZoneGraphSubsystem->GetWorld(), ReadyToUseLaneSphereLocation, 100.0f, 32, FColor::Magenta, false, LifeTime, 0, 10.0f);
			}
		}
	};

	if (GMassTrafficDebugLaneData >= 1)	// 1 = Debug lane data ([prev], current -> iterate left and right -> next, [next, next -> next], current splitting and merging)
	{
		// Display the previous lane, if it is valid.
		if (PrevLaneData != nullptr)
		{
			DrawLaneData(*PrevLaneData, FColor::Magenta, 10.0f);
		}
		
		DrawLaneData(*CurrentLaneData, FColor::Green, 15.0f);
		
		// Display the next lane and its successors, if the next lane is valid.
		if (NextLaneData != nullptr)
		{
			DrawLaneData(*NextLaneData, FColor::Yellow, 20.0f);
		
			for (FZoneGraphTrafficLaneData* NextLane : NextLaneData->NextLanes)
			{
				DrawLaneData(*NextLane, FColor::Orange, 25.0f);
			}
		}

		for (FZoneGraphTrafficLaneData* LeftLane = CurrentLaneData->LeftLane; LeftLane != nullptr; LeftLane = LeftLane->LeftLane)
		{
			DrawLaneData(*LeftLane, FColor::Blue, 30.0f);
			
			for (FZoneGraphTrafficLaneData* NextLeftLane : LeftLane->NextLanes)
			{
				DrawLaneData(*NextLeftLane, FColor::Cyan, 30.0f);
			}
		}
	
		for (FZoneGraphTrafficLaneData* RightLane = CurrentLaneData->RightLane; RightLane != nullptr; RightLane = RightLane->RightLane)
		{
			DrawLaneData(*RightLane, FColor::Red, 35.0f);
			
			for (FZoneGraphTrafficLaneData* NextRightLane : RightLane->NextLanes)
			{
				DrawLaneData(*NextRightLane, FColor::Purple, 35.0f);
			}
		}
		
		for (FZoneGraphTrafficLaneData* SplitLane : CurrentLaneData->SplittingLanes)
		{
			DrawLaneData(*SplitLane, FColor::White, 40.0f);
		}
		
		for (FZoneGraphTrafficLaneData* MergeLane : CurrentLaneData->MergingLanes)
		{
			DrawLaneData(*MergeLane, FColor::Silver, 45.0f);
		}
	}
}

void DrawDebugYieldLaneIndicators(const UMassTrafficSubsystem& MassTrafficSubsystem, const FZoneGraphLaneHandle& CurrentLaneHandle, const FZoneGraphLaneHandle* NextLaneHandle, const float DistanceAlongLane, const float LaneLength, const bool bShouldPreemptivelyYieldAtIntersection, const bool bShouldReactivelyYieldAtIntersection, const float LifeTime)
{
	if (GMassTrafficDebugYieldAtIntersectionBehavior == 0)	// 0 = Off (default)
	{
		return;
	}

	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();
	if (!ensureMsgf(MassTrafficSettings != nullptr, TEXT("Can't access MassTrafficSettings in DrawDebugYieldLaneIndicators.")))
	{
		return;
	}

	// We at least need the current lane data in order to draw any indicators.
	const FZoneGraphTrafficLaneData* CurrentLaneData = MassTrafficSubsystem.GetTrafficLaneData(CurrentLaneHandle);
	if (CurrentLaneData == nullptr)
	{
		return;
	}

	// But, the next lane is optional.
	const FZoneGraphTrafficLaneData* NextLaneData = NextLaneHandle != nullptr ? MassTrafficSubsystem.GetTrafficLaneData(*NextLaneHandle) : nullptr;

	const float DistanceFromIntersection = LaneLength - DistanceAlongLane;
	const bool bIsApproachingIntersection = NextLaneData != nullptr && NextLaneData->ConstData.bIsIntersectionLane && DistanceFromIntersection < MassTrafficSettings->MaxDistanceFromEndOfLaneForPreemptiveYield;

	// Only draw lines for Entities and their lanes if they're in the intersection or approaching the intersection.
	if (!CurrentLaneData->ConstData.bIsIntersectionLane && !bIsApproachingIntersection)
	{
		return;
	}
	
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(MassTrafficSubsystem.GetWorld());
	if (ZoneGraphSubsystem == nullptr)
	{
		return;
	}

	const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(CurrentLaneHandle.DataHandle);
	if (ZoneGraphStorage == nullptr)
	{
		return;
	}

	const auto& DrawLaneIndicator = [ZoneGraphSubsystem, ZoneGraphStorage, LifeTime](const FZoneGraphLaneHandle& LaneHandle, const FColor IndicatorColor, const float ZOffset = 10.0f)
	{
		if (!LaneHandle.IsValid())
		{
			return;
		}
		
		const FZoneLaneData& ZoneLaneData = ZoneGraphStorage->Lanes[LaneHandle.Index];
		const FVector& LaneStart = ZoneGraphStorage->LanePoints[ZoneLaneData.PointsBegin] + FVector(0.0f, 0.0f, ZOffset);
		const FVector& LaneEnd = ZoneGraphStorage->LanePoints[ZoneLaneData.PointsEnd - 1] + FVector(0.0f, 0.0f, ZOffset);
		
		DrawDebugDirectionalArrow(ZoneGraphSubsystem->GetWorld(), LaneStart, LaneEnd, 100000.0f, IndicatorColor, false, LifeTime, 0, 10.0f);
	};

	const auto& DrawSphereIndicator = [ZoneGraphSubsystem, ZoneGraphStorage, LifeTime, DistanceAlongLane](const FZoneGraphLaneHandle& LaneHandle, const FColor IndicatorColor, const float ZOffset = 10.0f)
	{
		if (!LaneHandle.IsValid())
		{
			return;
		}
		
		const FZoneLaneData& ZoneLaneData = ZoneGraphStorage->Lanes[LaneHandle.Index];
		
		float RemainingSquaredDistanceAlongLane = FMath::Square(DistanceAlongLane);

		for (int32 PointIndex = ZoneLaneData.PointsBegin; PointIndex < ZoneLaneData.PointsEnd; ++PointIndex)
		{
			const FVector& LaneSegmentStart = ZoneGraphStorage->LanePoints[ZoneLaneData.PointsBegin];
			const FVector& LaneSegmentEnd = ZoneGraphStorage->LanePoints[ZoneLaneData.PointsEnd - 1];

			const float LaneSegmentSquaredLength = (LaneSegmentEnd - LaneSegmentStart).SizeSquared();

			if (RemainingSquaredDistanceAlongLane <= LaneSegmentSquaredLength)
			{
				const FVector LaneSegmentDir = (LaneSegmentEnd - LaneSegmentStart).GetSafeNormal();
				const FVector IndicatorLocation = LaneSegmentStart + LaneSegmentDir * FMath::Sqrt(RemainingSquaredDistanceAlongLane) + FVector(0.0f, 0.0f, ZOffset);

				DrawDebugSphere(ZoneGraphSubsystem->GetWorld(), IndicatorLocation, 50.0f, 32, IndicatorColor, false, LifeTime, 0, 10.0f);
			}
			
			RemainingSquaredDistanceAlongLane -= LaneSegmentSquaredLength;
		}
	};

	if (GMassTrafficDebugYieldAtIntersectionBehavior >= 1)	// 1 = Draw lane indicators for yielding lane.  Yellow is pre-emptive yield.  Orange is reactive yield
	{
		const FColor IndicatorColor = bShouldPreemptivelyYieldAtIntersection ? FColor::Yellow : bShouldReactivelyYieldAtIntersection ? FColor::Orange : FColor::Green;
		
		if (bShouldPreemptivelyYieldAtIntersection || bShouldReactivelyYieldAtIntersection)
		{
			DrawLaneIndicator(CurrentLaneHandle, IndicatorColor, 100.0f);
		}
		
		DrawSphereIndicator(CurrentLaneHandle, IndicatorColor, 200.0f);
	}
}

#endif // WITH_MASSTRAFFIC_DEBUG

	
}
