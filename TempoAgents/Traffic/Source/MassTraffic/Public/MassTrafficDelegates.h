// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "MassTrafficSubsystem.h"

class AMassTrafficCoordinator;

namespace UE::MassTrafficDelegates
{

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnPreTrafficLaneDataChange, UMassTrafficSubsystem* /*MassTrafficSubsystem*/);
		extern MASSTRAFFIC_API FOnPreTrafficLaneDataChange OnPreTrafficLaneDataChange;

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnTrafficLaneDataChanged, UMassTrafficSubsystem* /*MassTrafficSubsystem*/);
		extern MASSTRAFFIC_API FOnTrafficLaneDataChanged OnTrafficLaneDataChanged;

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostInitTrafficIntersections, UMassTrafficSubsystem* /*MassTrafficSubsystem*/);
		extern MASSTRAFFIC_API FOnPostInitTrafficIntersections OnPostInitTrafficIntersections;

}
