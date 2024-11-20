// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "TempoBrightnessMeterComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOAGENTSSHARED_API UTempoBrightnessMeterComponent : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoBrightnessMeterComponent();

	void UpdateBrightness();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure)
	float GetBrightness() const;

protected:
	virtual void BeginPlay() override;

	// Update period in seconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float UpdatePeriod = 5.0;

	UPROPERTY(VisibleAnywhere)
	float Brightness = 0.0;

	UPROPERTY(EditAnywhere)
	FIntPoint SizeXY = FIntPoint(8, 8);

	FTimerHandle TimerHandle;

	TOptional<FRenderCommandFence> RenderFence;

	mutable FCriticalSection Mutex;
};
