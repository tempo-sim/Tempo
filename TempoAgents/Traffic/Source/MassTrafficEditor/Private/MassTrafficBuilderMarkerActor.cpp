// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficBuilderMarkerActor.h"

#include "Kismet/KismetMathLibrary.h"
#include "EditorViewportClient.h"
#include "Components/ArrowComponent.h"


AMassTrafficBuilderMarkerActor::AMassTrafficBuilderMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;
	ArrowComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("DebugArrow"));
	ArrowComponent->SetWorldRotation(FRotator(-90, 0,0));
	RootComponent = ArrowComponent;
}


void AMassTrafficBuilderMarkerActor::BeginPlay()
{
	Super::BeginPlay();
}
