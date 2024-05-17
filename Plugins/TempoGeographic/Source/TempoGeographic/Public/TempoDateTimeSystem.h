// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TempoDateTimeSystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDateTimeChanged, const FDateTime&, DateTime);

UCLASS(BlueprintType, Blueprintable)
class TEMPOGEOGRAPHIC_API ATempoDateTimeSystem : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FDateTimeChanged DateTimeChangedEvent;

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "TempoGeographic", meta = (WorldContext = "WorldContextObject"))
	static ATempoDateTimeSystem* GetTempoDateTimeSystem(UObject* WorldContextObject);

protected:
	const FDateTime& GetSimDateTime() const { return SimDateTime; }

	void AdvanceSimDateTime(const FTimespan& Timespan);

	void BroadcastDateTimeChanged() const;

	void SetDayCycleRelativeRate(float DayCycleRelativeRateIn) { DayCycleRelativeRate = DayCycleRelativeRateIn; }
	
	// The rate the geographic world time advances faster than real time
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DayCycleRelativeRate = 1.0;

	// The current geographic world date and time
	UPROPERTY(EditAnywhere)
	FDateTime SimDateTime = FDateTime(2024, 4, 3, 11, 0, 0, 0);

	friend class UTempoGeographicServiceSubsystem;
};
