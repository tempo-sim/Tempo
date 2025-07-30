// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TempoPlayerController.generated.h"

UCLASS(Blueprintable)
class TEMPOCORE_API ATempoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATempoPlayerController();

	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION(BlueprintCallable, Category = "Tempo")
	void ToggleMouseCaptured();

	UFUNCTION(BlueprintCallable, Category = "Tempo")
	void SetMouseCaptured(bool bCaptured);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tempo")
	bool bMouseCaptured = false;
};
