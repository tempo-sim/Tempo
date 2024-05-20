// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSensorInterface.h"

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

#include "TempoSceneCaptureComponent2D.generated.h"

UCLASS(Abstract)
class TEMPOSENSORSSHARED_API UTempoSceneCaptureComponent2D : public USceneCaptureComponent2D, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	UTempoSceneCaptureComponent2D();

	virtual int32 GetSensorId() const override { return SensorId; }

	virtual FString GetSensorName() const override { return GetName(); }

	virtual float GetRate() const override { return RateHz; }
	
	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const override { return MeasurementTypes; }
	
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;
	
protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TEnumAsByte<ETextureRenderTargetFormat> RenderTargetFormat;

	UPROPERTY(EditAnywhere)
	float RateHz = 10.0;

	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	UPROPERTY(EditAnywhere)
	FIntPoint SizeXY = FIntPoint(960, 540);

	UPROPERTY(VisibleAnywhere)
	int32 SensorId = 0;

	UPROPERTY(VisibleAnywhere)
	int32 SequenceId = 0;
	
private:
	void MaybeCapture();
	
	void InitRenderTarget();
	
	FTimerHandle TimerHandle;
};
