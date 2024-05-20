// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyClass.generated.h"

UCLASS()
class TEMPO_API AMyClass : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AMyClass();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
