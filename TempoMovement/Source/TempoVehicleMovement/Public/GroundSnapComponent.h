// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GroundSnapComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOVEHICLEMOVEMENT_API UGroundSnapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGroundSnapComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
	
protected:
	// If true, we will use the ExtentsOverride below rather than the owner's extents.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bOverrideOwnerExtents = false;

	// We will trace rays from the four corners indicated either by these extents, centered at the owner's location,
	// or the owner's extents, and determine the snapped location and rotation from the average of what we find.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition=bOverrideOwnerExtents))
	FVector2D ExtentsOverride = FVector2D(100.0, 100.0);

	// We will search this far above and below the Owner's current location for the ground.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float SearchDistance = 1000000.0; // 10km

	// If true, we will reject normals from surfaces steeper than MaxSlopeAngle.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bLimitSlopeAngle = true;

	// The surface angle, in degrees, above which we will reject normals.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(UIMin=0.0, UIMax=60.0, ClampMin=0.0, ClampMax=60.0, EditCondition=bLimitSnapAngle))
	float MaxSlopeAngle = 45.0;

	// If true, we will include hidden components in our extents calculation.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="!bOverrideOwnerExtents"))
	bool bIncludeHiddenComponentsInExtents = false;
};
