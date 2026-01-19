#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenCVLensDistortionParameters.h"

#include "TempoDistortion.generated.h"

UCLASS(Blueprintable)
class TEMPOCAMERA_API ATempoDistortion : public AActor
{
	GENERATED_BODY()
    
public:    
	ATempoDistortion();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	class UTextureRenderTarget2D* OutputRenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	bool bMatchViewportResolution = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo", meta = (EditCondition = "!bMatchViewportResolution"))
	FIntPoint ManualResolution;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	bool bUpdateEveryFrame = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	FOpenCVLensDistortionParameters LensParameters;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	float CroppingFactor = 0.0f;

private:
	void UpdateDistortionMap();
	
	class UTexture2D* GenerateTrueDistortionMap(const FIntPoint& InSize);
};