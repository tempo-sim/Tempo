// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficFragments.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficLaneChange.h"
#include "MassTrafficMovement.h"

#include "MassEntityView.h"
#include "MassCommandBuffer.h"
#include "MassZoneGraphNavigationFragments.h"

using namespace UE::MassTraffic;


DECLARE_DWORD_COUNTER_STAT(TEXT("Lane Changes In Progress"), STAT_Traffic_LaneChangesInProgress, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Lane Changes In Count Down"), STAT_Traffic_LaneChangesInCountDown, STATGROUP_Traffic);


//
// FMassTrafficLight
//


FVector FMassTrafficLight::GetXDirection() const
{
	const FRotator Rotator(0.0f, ZRotation, 0.0f);
	const FVector XDirection = Rotator.RotateVector(FVector::XAxisVector);
	return XDirection;
}


FColor FMassTrafficLight::GetDebugColorForVehicles() const
{
	if ((TrafficLightStateFlags & EMassTrafficLightStateFlags::VehicleGo) != EMassTrafficLightStateFlags::None)
	{
		return FColor::Green;
	}
	if ((TrafficLightStateFlags & EMassTrafficLightStateFlags::VehiclePrepareToStop) != EMassTrafficLightStateFlags::None)
	{
		return FColor::Yellow;
	}

	return FColor::Red;
}


FColor FMassTrafficLight::GetDebugColorForPedestrians(const EMassTrafficDebugTrafficLightSide Side) const
{
	if ((Side == EMassTrafficDebugTrafficLightSide::Front) &&
		((TrafficLightStateFlags & EMassTrafficLightStateFlags::PedestrianGo_FrontSide) != EMassTrafficLightStateFlags::None))
	{
		return FColor::Green;
	}
	if ((Side == EMassTrafficDebugTrafficLightSide::Left) &&
		((TrafficLightStateFlags & EMassTrafficLightStateFlags::PedestrianGo_LeftSide) != EMassTrafficLightStateFlags::None))
	{
		return FColor::Green;
	}
	if ((Side == EMassTrafficDebugTrafficLightSide::Right) &&
		((TrafficLightStateFlags & EMassTrafficLightStateFlags::PedestrianGo_RightSide) != EMassTrafficLightStateFlags::None))
	{
		return FColor::Green;
	}

	return FColor::Red;
}


//
// FMassTrafficLaneToTrafficLightMap
//


bool FMassTrafficLaneToTrafficLightMap::SetTrafficLightForLane(const FZoneGraphTrafficLaneData* VehicleTrafficLaneData, const int8 TrafficLightIndex)
{
	if (Map.Contains(VehicleTrafficLaneData))
	{
		if (Map[VehicleTrafficLaneData] == TrafficLightIndex)
		{
			return true;
		}
			
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Vehicle lane %d - Already controlled by intersection traffic light %d - New traffic light %d"), ANSI_TO_TCHAR(__FUNCTION__),
			VehicleTrafficLaneData->LaneHandle.Index, Map[VehicleTrafficLaneData], TrafficLightIndex);
		return false;
	}

	Map.Add(VehicleTrafficLaneData, TrafficLightIndex);
	return true;
}


bool FMassTrafficLaneToTrafficLightMap::SetTrafficLightForLanes(const TArray<FZoneGraphTrafficLaneData*>& VehicleTrafficLanes, const int8 TrafficLightIndex)
{
	bool OK = true;
	for (FZoneGraphTrafficLaneData* VehicleTrafficLaneData : VehicleTrafficLanes)
	{
		OK &= SetTrafficLightForLane(VehicleTrafficLaneData, TrafficLightIndex);
	}
	
	return OK;
}


int8 FMassTrafficLaneToTrafficLightMap::GetTrafficLightForLane(const FZoneGraphTrafficLaneData* VehicleTrafficLaneData) const
{
	if (!Map.Contains(VehicleTrafficLaneData))
	{
		return INDEX_NONE;
	}
		
	return Map[VehicleTrafficLaneData];
}


//
// FMassTrafficPeriod
//


bool FMassTrafficPeriod::AddTrafficLightControl(const int8 TrafficLightIndex, const EMassTrafficLightStateFlags TrafficLightStateFlags)
{
	if (TrafficLightIndex == INDEX_NONE)
	{
		return false;
	}
	
	if (TrafficLightControls.Num() <= TrafficLightIndex)
	{
		TrafficLightControls.SetNum(TrafficLightIndex + 1);
	}
	
	if (TrafficLightControls[TrafficLightIndex].bIsValid)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Period already has a traffic light control set for traffic light index %d."), ANSI_TO_TCHAR(__FUNCTION__), TrafficLightIndex);
		return false;
	}

	TrafficLightControls[TrafficLightIndex].bIsValid = true;
	TrafficLightControls[TrafficLightIndex].TrafficLightStateFlags = TrafficLightStateFlags;
	return true;
}


FMassTrafficLightControl* FMassTrafficPeriod::GetTrafficLightControl(const int8 TrafficLightIndex) 
{
	if (TrafficLightIndex == INDEX_NONE)
	{
		return nullptr;
	}
		
	if (TrafficLightIndex >= TrafficLightControls.Num() || !TrafficLightControls[TrafficLightIndex].bIsValid)
	{
		return nullptr;
	}

	return &TrafficLightControls[TrafficLightIndex];
}

bool FMassTrafficPeriod::VehicleLaneClosesInNextPeriod(FZoneGraphTrafficLaneData* VehicleLane) const
{
	// Not the most efficient code here.

	const int32 RealIndex = VehicleLanes.Find(VehicleLane);
	if (RealIndex == INDEX_NONE)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - For testing if 'open vehicle lane closes in next period', lane fragment was not found in 'open vehicle lanes'."),
			ANSI_TO_TCHAR(__builtin_FUNCTION()), RealIndex);
		return false;
	}
	if (RealIndex > 255)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - For testing if 'open vehicle lane closes in next period', index into 'open vehicle lanes' %d is bigger than max 255."),
			ANSI_TO_TCHAR(__builtin_FUNCTION()), RealIndex);
		return false;
	}

	return VehicleLaneIndices_ClosedInNextPeriod.Contains(RealIndex);
}


//
// FDataFragment_TrafficIntersection
//


void FMassTrafficLightIntersectionFragment::ApplyLanesActionToCurrentPeriod(
	const EMassTrafficControllerLanesAction VehicleLanesAction,
	const EMassTrafficControllerLanesAction PedestrianLanesAction,
	UMassCrowdSubsystem* MassCrowdSubsystem,
	const bool bForce) 
{
	const FMassTrafficPeriod& CurrentPeriod = GetCurrentPeriod();

	// Open or close all this period's vehicle lanes.
	// NOTE - These should all be intersection lanes.
	
	if ((VehicleLanesAction != LastVehicleLanesActionAppliedToCurrentPeriod || bForce) &&
		VehicleLanesAction != EMassTrafficControllerLanesAction::None)
	{
		for (int32 I = 0; I < CurrentPeriod.NumVehicleLanes(EMassTrafficIntersectionVehicleLaneType::VehicleLane); I++)
		{
			FZoneGraphTrafficLaneData* VehicleTrafficLaneData = CurrentPeriod.GetVehicleLane(I, EMassTrafficIntersectionVehicleLaneType::VehicleLane);

			if (!VehicleTrafficLaneData->ConstData.bIsIntersectionLane)
			{
				continue;
			}

			if (VehicleLanesAction == EMassTrafficControllerLanesAction::Open)
			{
				VehicleTrafficLaneData->bIsOpen = true;
				VehicleTrafficLaneData->bIsAboutToClose = false;
			}
			else if (VehicleLanesAction == EMassTrafficControllerLanesAction::HardClose)
			{
				VehicleTrafficLaneData->bIsOpen = false;
				VehicleTrafficLaneData->bIsAboutToClose = false;
			}
			else if (VehicleLanesAction == EMassTrafficControllerLanesAction::SoftClose)
			{
				VehicleTrafficLaneData->bIsOpen = !CurrentPeriod.VehicleLaneClosesInNextPeriod(VehicleTrafficLaneData);
				VehicleTrafficLaneData->bIsAboutToClose = false;
			}
			else if (VehicleLanesAction == EMassTrafficControllerLanesAction::HardPrepareToClose)
			{
				VehicleTrafficLaneData->bIsAboutToClose = true;										
			}
			else if (VehicleLanesAction == EMassTrafficControllerLanesAction::SoftPrepareToClose)
			{
				VehicleTrafficLaneData->bIsAboutToClose = CurrentPeriod.VehicleLaneClosesInNextPeriod(VehicleTrafficLaneData);
			}
		}
	
		LastVehicleLanesActionAppliedToCurrentPeriod = VehicleLanesAction;
	}

	
	if ((PedestrianLanesAction != LastPedestrianLanesActionAppliedToCurrentPeriod || bForce) &&
		PedestrianLanesAction != EMassTrafficControllerLanesAction::None)
	{
		// Open or close all this period's pedestrian lanes.
		// NOTE - These is no soft-close for these lanes.

		for (int32 CrosswalkLaneIndex : CurrentPeriod.CrosswalkLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkLaneIndex, ZoneGraphDataHandle);
			if (!LaneHandle.IsValid())
			{
				continue;
			}

			if (PedestrianLanesAction == EMassTrafficControllerLanesAction::Open)
			{
				MassCrowdSubsystem->SetLaneState(LaneHandle, ECrowdLaneState::Opened);			
			}
			else if (PedestrianLanesAction == EMassTrafficControllerLanesAction::HardClose ||
					PedestrianLanesAction == EMassTrafficControllerLanesAction::SoftClose)
			{
				MassCrowdSubsystem->SetLaneState(LaneHandle, ECrowdLaneState::Closed);			
			}
		}

		
		// Open or close all this period's pedestrian waiting lanes.
		// NOTE - These is no soft-close for these lanes.

		for (int32 CrosswalkWaitingLaneIndex : CurrentPeriod.CrosswalkWaitingLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkWaitingLaneIndex, ZoneGraphDataHandle);
			if (!LaneHandle.IsValid())
			{
				continue;
			}
			
			if (PedestrianLanesAction == EMassTrafficControllerLanesAction::Open)
			{
				MassCrowdSubsystem->SetLaneState(LaneHandle, ECrowdLaneState::Opened);			
			}
			else if (PedestrianLanesAction == EMassTrafficControllerLanesAction::HardClose ||
					PedestrianLanesAction == EMassTrafficControllerLanesAction::SoftClose)
			{
				MassCrowdSubsystem->SetLaneState(LaneHandle, ECrowdLaneState::Closed);			
			}
		}
		
		LastPedestrianLanesActionAppliedToCurrentPeriod = PedestrianLanesAction;
	}	
}


void FMassTrafficLightIntersectionFragment::UpdateTrafficLightsForCurrentPeriod()
{
	if (!bHasTrafficLights)
	{
		return;	
	}
	
	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>(); 

	FMassTrafficPeriod& CurrentPeriod = GetCurrentPeriod();
	
	for (int8 I = 0; I < TrafficLights.Num(); I++)
	{
		const FMassTrafficLightControl* CurrentTrafficLightControl = CurrentPeriod.GetTrafficLightControl(I);		
		if (!CurrentTrafficLightControl)
		{
			continue;
		}

		// Get the traffic light state flags from this control. We may modify it below before giving it to the traffic light.
		EMassTrafficLightStateFlags TrafficLightStateFlags = CurrentTrafficLightControl->TrafficLightStateFlags;

		// Modify the traffic light state flags to show a yellow light instead of a green light, if -
		//		(1) The current period is about to close.
		//		(2) *All* open vehicle lanes in the current period will close in the next period.
		if ((TrafficLightStateFlags & (EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::VehicleGoProtectedLeft)) != EMassTrafficLightStateFlags::None) // ..light wants to be green, but maybe it should be yellow
		{
			const bool bIsCurrentPeriodAboutToEnd =
				(CurrentPeriod.Duration < 2.0f * MassTrafficSettings->StandardTrafficPrepareToStopSeconds ? 
					PeriodTimeRemaining < CurrentPeriod.Duration / 2.0f :
					PeriodTimeRemaining < MassTrafficSettings->StandardTrafficPrepareToStopSeconds);
			
			if (bIsCurrentPeriodAboutToEnd)
			{
				TrafficLightStateFlags = PeriodTimeRemaining > 0.0f ? TrafficLightStateFlags | EMassTrafficLightStateFlags::VehiclePrepareToStop : TrafficLightStateFlags & ~EMassTrafficLightStateFlags::VehiclePrepareToStop;

				if (PeriodTimeRemaining <= 0.0f)
				{
					// If the current period has a protected left turn, ...
					if ((TrafficLightStateFlags & EMassTrafficLightStateFlags::VehicleGoProtectedLeft) != EMassTrafficLightStateFlags::None)
					{
						FMassTrafficPeriod& NextPeriod = GetNextPeriod();
						const FMassTrafficLightControl* NextTrafficLightControl = NextPeriod.GetTrafficLightControl(I);

						EMassTrafficLightStateFlags NextTrafficLightStateFlags = NextTrafficLightControl->TrafficLightStateFlags;

						// And, the next period does *not* have a protected left turn, ...
						if ((NextTrafficLightStateFlags & EMassTrafficLightStateFlags::VehicleGoProtectedLeft) == EMassTrafficLightStateFlags::None)
						{
							// Then, remove protected left turn flag.
							TrafficLightStateFlags &= ~EMassTrafficLightStateFlags::VehicleGoProtectedLeft;
						}
					}

					if (CurrentTrafficLightControl->bWillAllVehicleLanesCloseInNextPeriodForThisTrafficLight)
					{
						TrafficLightStateFlags &= ~EMassTrafficLightStateFlags::VehicleGo;
					}
				}
			}
		}

		
		// Give traffic light the (possibly modified) traffic light state.
		TrafficLights[I].TrafficLightStateFlags = TrafficLightStateFlags;
	}
}


void FMassTrafficLightIntersectionFragment::RestartIntersection(UMassCrowdSubsystem* MassCrowdSubsystem) 
{
	const uint8 CurrentPeriodIndex_Saved = CurrentPeriodIndex;

	PedestrianLightsShowStop();
	
	CurrentPeriodIndex = 0;
	
	for (uint8 PeriodIndex = 0; PeriodIndex < Periods.Num(); PeriodIndex++)
	{
		ApplyLanesActionToCurrentPeriod(EMassTrafficControllerLanesAction::HardClose, EMassTrafficControllerLanesAction::HardClose, MassCrowdSubsystem, true);
		AdvancePeriod();
	}
	
	CurrentPeriodIndex = CurrentPeriodIndex_Saved;

	PeriodTimeRemaining = 1.0f;
}


void FMassTrafficLightIntersectionFragment::Finalize(const FMassTrafficLaneToTrafficLightMap& LaneToTrafficLightMap)
{
	const uint8 NumPeriods = static_cast<uint8>(Periods.Num());
	for (uint8 P = 0; P < NumPeriods; P++)
	{			
		FMassTrafficPeriod& ThisPeriod = Periods[P];
		FMassTrafficPeriod& NextPeriod = Periods[(P + 1) % NumPeriods];

		for (int32 I = 0; I < ThisPeriod.NumVehicleLanes(EMassTrafficIntersectionVehicleLaneType::VehicleLane); I++)
		{
			const FZoneGraphTrafficLaneData* ThisTrafficLaneData = ThisPeriod.GetVehicleLane(I, EMassTrafficIntersectionVehicleLaneType::VehicleLane);

			if (NextPeriod.VehicleLanes.Contains(ThisTrafficLaneData))
			{
				const int8 TrafficLightIndex = LaneToTrafficLightMap.GetTrafficLightForLane(ThisTrafficLaneData);
				FMassTrafficLightControl* TrafficLightControl = ThisPeriod.GetTrafficLightControl(TrafficLightIndex);
				if (!TrafficLightControl)
				{
					continue;
				}
				TrafficLightControl->bWillAllVehicleLanesCloseInNextPeriodForThisTrafficLight = false;	
			}
			else
			{
				if (I > 255)
				{
					UE_LOG(LogMassTraffic, Error, TEXT("%s - For storing 'open vehicle lanes closed in next period', index into 'open vehicle lanes' %d is bigger than max 255."), ANSI_TO_TCHAR(__FUNCTION__), I);
					continue;
				}
				
				ThisPeriod.VehicleLaneIndices_ClosedInNextPeriod.Add(I);
			}
		}
	}
}

//
// FMassTrafficSignIntersectionSide
//

bool FMassTrafficSignIntersectionSide::AreAllVehicleIntersectionLanesOpen() const
{
	for (const auto& VehicleIntersectionLane : VehicleIntersectionLanes)
	{
		if (!VehicleIntersectionLane.Key->bIsOpen)
		{
			return false;
		}
	}

	return true;
}

bool FMassTrafficSignIntersectionSide::AreAllVehicleIntersectionLanesClosed() const
{
	for (const auto& VehicleIntersectionLane : VehicleIntersectionLanes)
	{
		if (VehicleIntersectionLane.Key->bIsOpen)
		{
			return false;
		}
	}

	return true;
}

bool FMassTrafficSignIntersectionSide::AreAllVehicleIntersectionLanesClear() const
{
	for (const auto& VehicleIntersectionLane : VehicleIntersectionLanes)
	{
		if (VehicleIntersectionLane.Key->NumVehiclesOnLane > 0)
		{
			return false;
		}
	}

	return true;
}

bool FMassTrafficSignIntersectionSide::AreAllCrosswalkLanesOpen(const FMassTrafficSignIntersectionFragment& IntersectionFragment, const UMassCrowdSubsystem& MassCrowdSubsystem) const
{
	for (const int32 CrosswalkLaneIndex : CrosswalkLanes)
	{
		const FZoneGraphLaneHandle CrosswalkLaneHandle(CrosswalkLaneIndex, IntersectionFragment.ZoneGraphDataHandle);
		if (!ensureMsgf(CrosswalkLaneHandle.IsValid(), TEXT("Must get valid CrosswalkLaneHandle in FMassTrafficSignIntersectionSide::AreAllCrosswalkLanesOpen.")))
		{
			return false;
		}

		const ECrowdLaneState CrosswalkLaneState = MassCrowdSubsystem.GetLaneState(CrosswalkLaneHandle);

		if (CrosswalkLaneState != ECrowdLaneState::Opened)
		{
			return false;
		}
	}

	return true;
}

bool FMassTrafficSignIntersectionSide::AreAllCrosswalkLanesClosed(const FMassTrafficSignIntersectionFragment& IntersectionFragment, const UMassCrowdSubsystem& MassCrowdSubsystem) const
{
	for (const int32 CrosswalkLaneIndex : CrosswalkLanes)
	{
		const FZoneGraphLaneHandle CrosswalkLaneHandle(CrosswalkLaneIndex, IntersectionFragment.ZoneGraphDataHandle);
		if (!ensureMsgf(CrosswalkLaneHandle.IsValid(), TEXT("Must get valid CrosswalkLaneHandle in FMassTrafficSignIntersectionSide::AreAllCrosswalkLanesClosed.")))
		{
			return false;
		}

		const ECrowdLaneState CrosswalkLaneState = MassCrowdSubsystem.GetLaneState(CrosswalkLaneHandle);

		if (CrosswalkLaneState != ECrowdLaneState::Closed)
		{
			return false;
		}
	}

	return true;
}

bool FMassTrafficSignIntersectionSide::AreAllCrosswalkLanesClear(const FMassTrafficSignIntersectionFragment& IntersectionFragment, const UMassCrowdSubsystem& MassCrowdSubsystem) const
{
	for (const int32 CrosswalkLaneIndex : CrosswalkLanes)
	{
		const FZoneGraphLaneHandle CrosswalkLaneHandle(CrosswalkLaneIndex, IntersectionFragment.ZoneGraphDataHandle);
		if (!ensureMsgf(CrosswalkLaneHandle.IsValid(), TEXT("Must get valid CrosswalkLaneHandle in FMassTrafficSignIntersectionSide::AreAllCrosswalkLanesClear.")))
		{
			return false;
		}

		const FCrowdTrackingLaneData* CrosswalkLaneData = MassCrowdSubsystem.GetCrowdTrackingLaneData(CrosswalkLaneHandle);
		
		if (!ensureMsgf(CrosswalkLaneData != nullptr, TEXT("Must get valid CrosswalkLaneData in FMassTrafficSignIntersectionSide::AreAllCrosswalkLanesClear.")))
		{
			return false;
		}

		if (CrosswalkLaneData->NumEntitiesOnLane > 0)
		{
			return false;
		}
	}

	return true;
}

bool FMassTrafficSignIntersectionSide::AreAllCrosswalkWaitingLanesClear(const FMassTrafficSignIntersectionFragment& IntersectionFragment, const UMassCrowdSubsystem& MassCrowdSubsystem) const
{
	for (const int32 CrosswalkWaitingLaneIndex : CrosswalkWaitingLanes)
	{
		const FZoneGraphLaneHandle CrosswalkWaitingLaneHandle(CrosswalkWaitingLaneIndex, IntersectionFragment.ZoneGraphDataHandle);
		if (!ensureMsgf(CrosswalkWaitingLaneHandle.IsValid(), TEXT("Must get valid CrosswalkWaitingLaneHandle in FMassTrafficSignIntersectionSide::AreAllCrosswalkWaitingLanesClear.")))
		{
			return false;
		}

		const FCrowdTrackingLaneData* CrosswalkWaitingLaneData = MassCrowdSubsystem.GetCrowdTrackingLaneData(CrosswalkWaitingLaneHandle);
		
		if (!ensureMsgf(CrosswalkWaitingLaneData != nullptr, TEXT("Must get valid CrosswalkLaneData in FMassTrafficSignIntersectionSide::AreAllCrosswalkWaitingLanesClear.")))
		{
			return false;
		}

		if (CrosswalkWaitingLaneData->NumEntitiesOnLane > 0)
		{
			return false;
		}
	}

	return true;
}

bool FMassTrafficSignIntersectionSide::AreAllEntitiesOnCrosswalkYielding(const FMassTrafficSignIntersectionFragment& IntersectionFragment, const UMassCrowdSubsystem& MassCrowdSubsystem, const UMassTrafficSubsystem& MassTrafficSubsystem) const
{
	for (const int32 CrosswalkLaneIndex : CrosswalkLanes)
	{
		const FZoneGraphLaneHandle CrosswalkLaneHandle(CrosswalkLaneIndex, IntersectionFragment.ZoneGraphDataHandle);
		if (!ensureMsgf(CrosswalkLaneHandle.IsValid(), TEXT("Must get valid CrosswalkLaneHandle in FMassTrafficSignIntersectionSide::AreAllEntitiesOnCrosswalkYielding.")))
		{
			return false;
		}
		
		const FCrowdTrackingLaneData* CrosswalkLaneData = MassCrowdSubsystem.GetCrowdTrackingLaneData(CrosswalkLaneHandle);
		
		if (!ensureMsgf(CrosswalkLaneData != nullptr, TEXT("Must get valid CrosswalkLaneData in FMassTrafficSignIntersectionSide::AreAllEntitiesOnCrosswalkYielding.")))
		{
			return false;
		}

		const FMassTrafficCrosswalkLaneInfo* CrosswalkLaneInfo = MassTrafficSubsystem.GetCrosswalkLaneInfo(CrosswalkLaneHandle);
		
		if (!ensureMsgf(CrosswalkLaneInfo != nullptr, TEXT("Must get valid CrosswalkLaneInfo in FMassTrafficSignIntersectionSide::AreAllEntitiesOnCrosswalkYielding.")))
		{
			return false;
		}

		const int32 NumEntitiesYieldingOnCrosswalkLane = CrosswalkLaneInfo->GetNumEntitiesYieldingOnCrosswalkLane();
		
		if (NumEntitiesYieldingOnCrosswalkLane != CrosswalkLaneData->NumEntitiesOnLane)
		{
			return false;
		}
	}

	return true;
}

//
// FMassTrafficSignIntersectionFragment
//

void FMassTrafficSignIntersectionFragment::RestartIntersection(UMassCrowdSubsystem& MassCrowdSubsystem) 
{
	LastVehicleLanesActionAppliedToSide.SetNum(IntersectionSides.Num());
	LastPedestrianLanesActionAppliedToSide.SetNum(IntersectionSides.Num());
	
	for (int32 IntersectionSideIndex = 0; IntersectionSideIndex < IntersectionSides.Num(); ++IntersectionSideIndex)
	{
		LastVehicleLanesActionAppliedToSide[IntersectionSideIndex] = EMassTrafficControllerLanesAction::None;
		LastPedestrianLanesActionAppliedToSide[IntersectionSideIndex] = EMassTrafficControllerLanesAction::None;
	}
	
	for (FMassTrafficSignIntersectionSide& IntersectionSide : IntersectionSides)
	{
		if (IntersectionSide.TrafficControllerSignType == EMassTrafficControllerSignType::None)
		{
			// Note:  We'd like to close the crosswalk lanes, here.  However, if we do, that triggers pedestrians to start queueing up on the crosswalk waiting lanes.
			// So, for now, we're just passing a PedestrianLanesAction of EMassTrafficControllerLanesAction::None.  This will prevent the pedestrians from queuing up
			// on the crosswalk waiting lanes.  But, it also results in a side effect where occasionally pedestrians walk up to the crosswalk waiting lanes
			// and immediately turn around and walk somewhere else.  But, this is fine for now.
			ApplyLanesActionToIntersectionSide(IntersectionSide, EMassTrafficControllerLanesAction::Open, EMassTrafficControllerLanesAction::None, MassCrowdSubsystem, true);
		}
		else if (IntersectionSide.TrafficControllerSignType == EMassTrafficControllerSignType::StopSign)
		{
			ApplyLanesActionToIntersectionSide(IntersectionSide, EMassTrafficControllerLanesAction::Open, EMassTrafficControllerLanesAction::Open, MassCrowdSubsystem, true);
			IntersectionSide.StopSignIntersectionState = EMassTrafficStopSignIntersectionState::CrosswalkOpen;
		}
		else if (IntersectionSide.TrafficControllerSignType == EMassTrafficControllerSignType::YieldSign)
		{
			ApplyLanesActionToIntersectionSide(IntersectionSide, EMassTrafficControllerLanesAction::Open, EMassTrafficControllerLanesAction::HardClose, MassCrowdSubsystem, true);
			IntersectionSide.YieldSignIntersectionState = EMassTrafficYieldSignIntersectionState::CrosswalkClosed;
		}
	}
}

void FMassTrafficSignIntersectionFragment::ApplyLanesActionToIntersectionSide(
	const FMassTrafficSignIntersectionSide& IntersectionSide,
	const EMassTrafficControllerLanesAction VehicleLanesAction,
	const EMassTrafficControllerLanesAction PedestrianLanesAction,
	UMassCrowdSubsystem& MassCrowdSubsystem,
	const bool bForce) 
{
	const EMassTrafficControllerLanesAction LastAppliedVehicleLanesAction = LastVehicleLanesActionAppliedToSide.IsValidIndex(IntersectionSide.IntersectionSideIndex)
		? LastVehicleLanesActionAppliedToSide[IntersectionSide.IntersectionSideIndex]
		: EMassTrafficControllerLanesAction::None;
	
	if ((VehicleLanesAction != LastAppliedVehicleLanesAction || bForce) &&
		VehicleLanesAction != EMassTrafficControllerLanesAction::None)
	{
		for (auto& VehicleIntersectionLane : IntersectionSide.VehicleIntersectionLanes)
		{
			if (!VehicleIntersectionLane.Key->ConstData.bIsIntersectionLane)
			{
				continue;
			}

			// Vehicle intersection lanes at traffic sign intersections can only be in a state of "open" or "closed".
			if (VehicleLanesAction == EMassTrafficControllerLanesAction::Open)
			{
				VehicleIntersectionLane.Key->bIsOpen = true;
				VehicleIntersectionLane.Key->bIsAboutToClose = false;
			}
			else if (VehicleLanesAction == EMassTrafficControllerLanesAction::HardClose
					|| VehicleLanesAction == EMassTrafficControllerLanesAction::SoftClose
					|| VehicleLanesAction == EMassTrafficControllerLanesAction::HardPrepareToClose
					|| VehicleLanesAction == EMassTrafficControllerLanesAction::SoftPrepareToClose)
			{
				VehicleIntersectionLane.Key->bIsOpen = false;
				VehicleIntersectionLane.Key->bIsAboutToClose = false;
			}
		}
		
		if (LastVehicleLanesActionAppliedToSide.IsValidIndex(IntersectionSide.IntersectionSideIndex))
		{
			LastVehicleLanesActionAppliedToSide[IntersectionSide.IntersectionSideIndex] = VehicleLanesAction;
		}
	}
	
	const EMassTrafficControllerLanesAction LastAppliedPedestrianLanesAction = LastPedestrianLanesActionAppliedToSide.IsValidIndex(IntersectionSide.IntersectionSideIndex)
		? LastPedestrianLanesActionAppliedToSide[IntersectionSide.IntersectionSideIndex]
		: EMassTrafficControllerLanesAction::None;
	
	if ((PedestrianLanesAction != LastAppliedPedestrianLanesAction || bForce) &&
		PedestrianLanesAction != EMassTrafficControllerLanesAction::None)
	{
		for (int32 CrosswalkLaneIndex : IntersectionSide.CrosswalkLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkLaneIndex, ZoneGraphDataHandle);
			if (!LaneHandle.IsValid())
			{
				continue;
			}
			
			if (PedestrianLanesAction == EMassTrafficControllerLanesAction::Open)
			{
				MassCrowdSubsystem.SetLaneState(LaneHandle, ECrowdLaneState::Opened);			
			}
			else if (PedestrianLanesAction == EMassTrafficControllerLanesAction::HardClose
					|| PedestrianLanesAction == EMassTrafficControllerLanesAction::SoftClose
					|| PedestrianLanesAction == EMassTrafficControllerLanesAction::HardPrepareToClose
					|| PedestrianLanesAction == EMassTrafficControllerLanesAction::SoftPrepareToClose)
			{
				MassCrowdSubsystem.SetLaneState(LaneHandle, ECrowdLaneState::Closed);			
			}
		}

		for (int32 CrosswalkWaitingLaneIndex : IntersectionSide.CrosswalkWaitingLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkWaitingLaneIndex, ZoneGraphDataHandle);
			if (!LaneHandle.IsValid())
			{
				continue;
			}
			
			if (PedestrianLanesAction == EMassTrafficControllerLanesAction::Open)
			{
				MassCrowdSubsystem.SetLaneState(LaneHandle, ECrowdLaneState::Opened);			
			}
			else if (PedestrianLanesAction == EMassTrafficControllerLanesAction::HardClose
					|| PedestrianLanesAction == EMassTrafficControllerLanesAction::SoftClose
					|| PedestrianLanesAction == EMassTrafficControllerLanesAction::HardPrepareToClose
					|| PedestrianLanesAction == EMassTrafficControllerLanesAction::SoftPrepareToClose)
			{
				MassCrowdSubsystem.SetLaneState(LaneHandle, ECrowdLaneState::Closed);			
			}
		}

		if (LastPedestrianLanesActionAppliedToSide.IsValidIndex(IntersectionSide.IntersectionSideIndex))
		{
			LastPedestrianLanesActionAppliedToSide[IntersectionSide.IntersectionSideIndex] = PedestrianLanesAction;
		}
	}	
}


//
// FDataFragment_TrafficVehicleLaneChange
//


void FMassTrafficVehicleLaneChangeFragment::SetLaneChangeCountdownSecondsToBeAtLeast(
	const UMassTrafficSettings& MassTrafficSettings,
	const EMassTrafficLaneChangeCountdownSeconds LaneChangeCountdownSecondsType,
	const FRandomStream& RandomStream
) 
{
	if (LaneChangeCountdownSeconds > 0.0f)
	{
		return;	
	}

	int32 SafetyCounter = 9;
	while (LaneChangeCountdownSeconds <= 0.0f && SafetyCounter >= 0)
	{
		if (LaneChangeCountdownSecondsType == EMassTrafficLaneChangeCountdownSeconds::AsNewTryUsingSettings)
		{
			LaneChangeCountdownSeconds = LaneChangeCountdownSeconds + FMath::Lerp(MassTrafficSettings.MinSecondsUntilLaneChangeDecision, MassTrafficSettings.MaxSecondsUntilLaneChangeDecision, RandomStream.FRand());
		}
		else if (LaneChangeCountdownSecondsType == EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings)
		{
			LaneChangeCountdownSeconds = LaneChangeCountdownSeconds + MassTrafficSettings.LaneChangeRetrySeconds;
		}
		else if (LaneChangeCountdownSecondsType == EMassTrafficLaneChangeCountdownSeconds::AsRetryOneSecond)
		{
			LaneChangeCountdownSeconds = LaneChangeCountdownSeconds + 1.0f;
		}
		else if (LaneChangeCountdownSecondsType == EMassTrafficLaneChangeCountdownSeconds::AsRetryOneHalfSecond)
		{
			LaneChangeCountdownSeconds = LaneChangeCountdownSeconds + 0.5f;
		}
		else if (LaneChangeCountdownSecondsType == EMassTrafficLaneChangeCountdownSeconds::AsRetryOneTenthSecond)
		{
			LaneChangeCountdownSeconds = LaneChangeCountdownSeconds + 0.1f;
		}

		--SafetyCounter;
	}
}


bool FMassTrafficVehicleLaneChangeFragment::AddOtherLaneChangeNextVehicle_ForVehicleBehind(
	FMassEntityHandle InVehicleEntity_Behind,
	FMassEntityManager& EntityManager)
{
	if (!InVehicleEntity_Behind.IsSet())
	{
		return true; // ..this is ok
	}
	
	if (!VehicleEntity_Current.IsSet()) // ..sanity, bad if this happens
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Lane Change Fragment - No current entity."),
			ANSI_TO_TCHAR(__FUNCTION__));
		return false;	
	}

	
	if (OtherVehicleEntities_Behind.IsFull())
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Lane Change Fragment - Entity list for other vehicles behind too full (%d) to add."),
			ANSI_TO_TCHAR(__FUNCTION__), OtherVehicleEntities_Behind.Max());
		return false;
	}

	if (VehicleEntity_Initial_Behind == InVehicleEntity_Behind)
	{
		UE_LOG(LogMassTraffic, Warning, TEXT("%s - Lane Change Fragment - Entity already is vehicle behind. This should never happen."),
			ANSI_TO_TCHAR(__FUNCTION__));		
		return false;		
	}
	
	if (OtherVehicleEntities_Behind.Contains(InVehicleEntity_Behind))
	{
		UE_LOG(LogMassTraffic, Warning, TEXT("%s - Lane Change Fragment - Entity already in entity list for other vechiles behind. This should never happen."),
			ANSI_TO_TCHAR(__FUNCTION__));		
		return false;
	}

	OtherVehicleEntities_Behind.AddUnique(InVehicleEntity_Behind);
	
	FMassEntityView EntityView_Behind(EntityManager, InVehicleEntity_Behind);
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Behind = EntityView_Behind.GetFragmentData<FMassTrafficNextVehicleFragment>();
	NextVehicleFragment_Behind.AddLaneChangeNextVehicle(VehicleEntity_Current);
	
	
	return true;
}


bool FMassTrafficVehicleLaneChangeFragment::BeginLaneChangeProgression(
	//int32 InDebugLabel,
	const EMassTrafficLaneChangeSide InLaneChangeSide,
	const float InDistanceAlongLaneForLaneChange_Final_Begin, const float InDistanceAlongLaneForLaneChange_Final_End,
	const float InDistanceBetweenLanes_Begin_ForActiveLaneChanges,
	// Fragments..
	const FTransformFragment& VehicleTransformFragment_Current,
	FMassTrafficVehicleLightsFragment& VehicleLightsFragment_Current,
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	const FMassZoneGraphLaneLocationFragment& LaneLocationFragment_Current, FZoneGraphTrafficLaneData* InTrafficLaneData_Initial,
	FZoneGraphTrafficLaneData* InTrafficLaneData_Final,
	// Other vehicles involved in lane change..
	const FMassEntityHandle InVehicleEntity_Current, const FMassEntityHandle InVehicleEntity_Initial_Behind,
	const FMassEntityHandle InVehicleEntity_Initial_Ahead, const FMassEntityHandle InVehicleEntity_Final_Behind,
	const FMassEntityHandle InVehicleEntity_Final_Ahead,
	// Other..
	FMassEntityManager& EntityManager)
{
	//DebugLabel = InDebugLabel;

	
	// Should not have been called under these circumstances! But we need to check if..
	//		(1) Lane change is already in progress.
	//		(2) Trying to go no way or both ways.
	//		(3) Necessary lane pointers are null.
	// This is bad, because vehicle has already been moved to another lane.
	
	if (IsLaneChangeInProgress())
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not begin lane change progression! Lane change is already in progress."), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	
	if (InLaneChangeSide != EMassTrafficLaneChangeSide::IsLaneChangingToTheLeft && InLaneChangeSide != EMassTrafficLaneChangeSide::IsLaneChangingToTheRight /*should never happen*/)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not begin lane change progression! Bad lane change state requested (%d)."), ANSI_TO_TCHAR(__FUNCTION__), int(InLaneChangeSide));
		return false;
	}
	
	if (!InTrafficLaneData_Initial || !InTrafficLaneData_Final /*should never happen*/)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not begin lane change progression! Incoming lane fragments are missing. Inital:0x%x Final:0x%x"), ANSI_TO_TCHAR(__FUNCTION__),
			TrafficLaneData_Initial, TrafficLaneData_Final);
		return false;
	}

	
	// Set simple values.

	DistanceBetweenLanes_Begin = InDistanceBetweenLanes_Begin_ForActiveLaneChanges;
	VehicleEntity_Current = InVehicleEntity_Current; 

	
	// Add lane change next vehicle fragments.

	VehicleEntity_Initial_Behind = InVehicleEntity_Initial_Behind;
	if (VehicleEntity_Initial_Behind.IsSet())
	{
		const FMassEntityView EntityView_Behind(EntityManager, VehicleEntity_Initial_Behind);
		FMassTrafficNextVehicleFragment& NextVehicleFragment_Behind = EntityView_Behind.GetFragmentData<FMassTrafficNextVehicleFragment>();
		NextVehicleFragment_Behind.AddLaneChangeNextVehicle(VehicleEntity_Current);
	}
	
	VehicleEntity_Initial_Ahead = InVehicleEntity_Initial_Ahead;
	if (VehicleEntity_Initial_Ahead.IsSet())
	{
		NextVehicleFragment_Current.AddLaneChangeNextVehicle(VehicleEntity_Initial_Ahead);
	}
	
	
	// Lane change progression can begin. Setting these values will make it begin.

	TrafficLaneData_Initial = InTrafficLaneData_Initial;
	TrafficLaneData_Final = InTrafficLaneData_Final;

	LaneChangeSide = InLaneChangeSide;

	DistanceAlongLane_Final_Begin = InDistanceAlongLaneForLaneChange_Final_Begin;
	DistanceAlongLane_Final_End = InDistanceAlongLaneForLaneChange_Final_End;
	Yaw_Initial = VehicleTransformFragment_Current.GetTransform().GetRotation().Euler().Z;


	// Switch on turn signals.	

	VehicleLightsFragment_Current.bLeftTurnSignalLights = (LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheLeft);
	VehicleLightsFragment_Current.bRightTurnSignalLights = (LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheRight);


	// Set a ghost vehicle on the initial lane, so new vehicles coming on to the lane know to avoid this lane changing
	// vehicle.

	{
		// If it's set now - set it only if it's closer to beginning of the lane than the vehicle it's currently set to.
		// If it's not set now - set it.
		if (TrafficLaneData_Initial->GhostTailVehicle_FromLaneChangingVehicle.IsSet())
		{
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment_LaneChangeGhost_Initial = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(TrafficLaneData_Initial->GhostTailVehicle_FromLaneChangingVehicle);
			if (LaneLocationFragment_LaneChangeGhost_Initial.DistanceAlongLane > LaneLocationFragment_Current.DistanceAlongLane)
			{
				TrafficLaneData_Initial->GhostTailVehicle_FromLaneChangingVehicle = VehicleEntity_Current;
			}
		}
		else
		{
			TrafficLaneData_Initial->GhostTailVehicle_FromLaneChangingVehicle = VehicleEntity_Current;			
		}
	}

	
	// The initial and final lanes are involved in a lane change.
	// (See all LANECHANGEONOFF.)

	if (TrafficLaneData_Initial) // ..sanity
	{
		++TrafficLaneData_Initial->NumVehiclesLaneChangingOffOfLane;
	}

	if (TrafficLaneData_Final) // ..sanity
	{
		++TrafficLaneData_Final->NumVehiclesLaneChangingOntoLane;
	}

	
	return true;
}


void FMassTrafficVehicleLaneChangeFragment::UpdateLaneChange(
	const float DeltaTimeSeconds,
	FMassTrafficVehicleLightsFragment& VehicleLightsFragment_Current,
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment_Current,
	const FMassEntityManager& EntityManager,
	const UMassTrafficSettings& MassTrafficSettings,
	const FRandomStream& RandomStream
)
{
	// If this has never been updated, run some initializations.
	if (LaneChangeCountdownSeconds == LaneChangeCountdownSeconds_Uninitialized)
	{
		LaneChangeCountdownSeconds = MassTrafficSettings.MaxSecondsUntilLaneChangeDecision * RandomStream.FRand();
		
		return; // ..and avoid possible giant delta time that often goes along with a first update
	}

	
	++StaggeredSleepCounterForStartNewLaneChanges;

	
	// Update, depending on state.
	if (IsLaneChangeInProgress())
	{
		INC_DWORD_STAT(STAT_Traffic_LaneChangesInProgress);

		// Only active lane changes have a 'progression' and are allowed to end themselves when they are done. Passive
		// lane changes are stopped externally when then lane ends.
		if (ZoneGraphLaneLocationFragment_Current.DistanceAlongLane > DistanceAlongLane_Final_End)
		{
			EndLaneChangeProgression(VehicleLightsFragment_Current, NextVehicleFragment_Current, EntityManager);

			SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsNewTryUsingSettings, RandomStream); // ..not handled by Reset()
		}
	}
	else if (LaneChangeCountdownSeconds > 0.0f)
	{
		INC_DWORD_STAT(STAT_Traffic_LaneChangesInCountDown);
		
		LaneChangeCountdownSeconds = LaneChangeCountdownSeconds - DeltaTimeSeconds;
	}
}


void FMassTrafficVehicleLaneChangeFragment::EndLaneChangeProgression(
	FMassTrafficVehicleLightsFragment& VehicleLightsFragment_Current,
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	const FMassEntityManager& EntityManager)
{
	// Turn off turn signals.
	
	VehicleLightsFragment_Current.bLeftTurnSignalLights = false;
	VehicleLightsFragment_Current.bRightTurnSignalLights = false;

	
	// Remove all next vehicle fragments we manage.
	
	if (VehicleEntity_Initial_Ahead.IsSet())
	{
		NextVehicleFragment_Current.RemoveLaneChangeNextVehicle(VehicleEntity_Initial_Ahead);
		
		VehicleEntity_Initial_Ahead.Reset();
	}


	// If we have behind vehicles, clear them.
	// This also means getting their next vehicle fragments, and removing the current vehicle as their next.

	// Only active lane changes clear the next vehicle (us) off the vehicles behind it.
	
	if (VehicleEntity_Initial_Behind.IsSet())
	{
		FMassEntityView EntityView_Other(EntityManager, VehicleEntity_Initial_Behind);
		FMassTrafficNextVehicleFragment& NextVehicleFragment_Other = EntityView_Other.GetFragmentData<FMassTrafficNextVehicleFragment>();

		NextVehicleFragment_Other.RemoveLaneChangeNextVehicle(VehicleEntity_Current);
	}
	
	if (!OtherVehicleEntities_Behind.IsEmpty())
	{
		for (FMassEntityHandle OtherVehicleEntity : OtherVehicleEntities_Behind)
		{
			if (!OtherVehicleEntity.IsSet())
			{
				continue;
			}

			FMassEntityView EntityView_Other(EntityManager, OtherVehicleEntity);
			FMassTrafficNextVehicleFragment& NextVehicleFragment_Other = EntityView_Other.GetFragmentData<FMassTrafficNextVehicleFragment>();

			NextVehicleFragment_Other.RemoveLaneChangeNextVehicle(VehicleEntity_Current);
		}

		OtherVehicleEntities_Behind.Empty();
	}
	

	// If the current vehicle is the ghost tail on the initial lane, we should clear it.
	
	if (TrafficLaneData_Initial && TrafficLaneData_Initial->GhostTailVehicle_FromLaneChangingVehicle == VehicleEntity_Current)
	{
		TrafficLaneData_Initial->GhostTailVehicle_FromLaneChangingVehicle = FMassEntityHandle();
	}


	// Decrement lane changing reference counts from lanes involved.
	// (See all LANECHANGEONOFF.)

	if (TrafficLaneData_Initial) // ..because reset can be called when a lane change hasn't happened 
	{
		--TrafficLaneData_Initial->NumVehiclesLaneChangingOffOfLane;
	}
	
	if (TrafficLaneData_Final) // ..because reset can be called when a lane change hasn't happened 
	{
		--TrafficLaneData_Final->NumVehiclesLaneChangingOntoLane;
	}
	
	
	// Clear remaining members.
	
	//DebugLabel = 0;
	LaneChangeSide = EMassTrafficLaneChangeSide::IsNotLaneChanging;
	DistanceBetweenLanes_Begin = 0.0f;
	DistanceAlongLane_Final_Begin = 0.0f;
	DistanceAlongLane_Final_End = 0.0f;
	VehicleEntity_Current = FMassEntityHandle();
	TrafficLaneData_Initial = nullptr;
	TrafficLaneData_Final = nullptr;
}

bool FMassTrafficNextVehicleFragment::AddLaneChangeNextVehicle(const FMassEntityHandle Entity_Current)
{
	if (NextVehicles_LaneChange.IsFull())
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - NextVehicles_LaneChange - Too full (at %d) to add to."),
			ANSI_TO_TCHAR(__builtin_FUNCTION()), NextVehicles_LaneChange.Max());
		return false;
	}

	// NOTE - It's OK if the same vehicle is already in the list.
	// This means that two vehicles have started changing lanes, on in front of the other. The one in front added it to
	// the one behind it, and the one behind added it for the one in front of it. Both of these are meant to serve the
	// same purpose. Whichever one finishes its lane change first will remove it.
	NextVehicles_LaneChange.AddUnique(Entity_Current);

	return true;
}
