// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoTimeServiceSubsystem.h"
#include "TempoTimeROSBridgeSubsystem.generated.h"

UCLASS()
class TEMPOTIMEROSBRIDGE_API UTempoTimeROSBridgeSubsystem : public UTempoTimeServiceSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// Overload that adapts the empty-payload ROS Step service to the proto-level StepRequest signature.
	void Step(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

protected:
	UPROPERTY()
	class UTempoROSNode* ROSNode;
};
