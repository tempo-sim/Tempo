// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ArrowComponent.h"

#include "MassTrafficBuilderMarkerActor.generated.h"


UCLASS(BlueprintType)
class MASSTRAFFICEDITOR_API AMassTrafficBuilderMarkerActor : public AActor
{
	GENERATED_BODY()

public:	

	AMassTrafficBuilderMarkerActor();

	UPROPERTY()
	UArrowComponent* ArrowComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bUseGroundRotation = false;

	UPROPERTY(EditAnywhere, meta=(MultiLine=true))
	FText ErrorDescription;
	
protected:

	virtual void BeginPlay() override;

};
