// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GroundSnapComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOMOVEMENT_API UGroundSnapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGroundSnapComponent();

	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

protected:
	// If true, we will use the ExtentsOverride below rather than the owner's extents.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bOverrideOwnerExtents = false;

	// We will trace rays from the four corners indicated either by these extents or the owner's extents, and
	// fit the ground plane to what we find. These are unscaled owner-local extents, like the owner's own
	// extents, so the owner's scale is applied on top of them.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition=bOverrideOwnerExtents))
	FVector2D ExtentsOverride = FVector2D(100.0, 100.0);

	// The owner-local XY point the ExtentsOverride above is centered on. Zero (the default) centers it on the
	// owner's origin. Use this when the owner's origin is not in the middle of the footprint you want to trace,
	// e.g. an origin at the rear axle of a vehicle. Only used when bOverrideOwnerExtents is true.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition=bOverrideOwnerExtents))
	FVector2D ExtentsCenter = FVector2D::ZeroVector;

	// We will search this far above and below the Owner's current location for the ground.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float SearchDistance = 1000000.0; // 10km

	// If true, we will not tilt the owner further than MaxSlopeAngle to match the ground.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bLimitSlopeAngle = true;

	// The angle, in degrees, past which we stop tilting the owner to match the ground. This limits the fitted
	// ground plane as a whole: the plane is fit to all four corners at once, and no corner is judged alone.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(UIMin=0.0, UIMax=60.0, ClampMin=0.0, ClampMax=60.0, EditCondition=bLimitSlopeAngle))
	float MaxSlopeAngle = 45.0;

	// If true, we will include hidden components in our extents calculation.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="!bOverrideOwnerExtents"))
	bool bIncludeHiddenComponentsInExtents = false;

	// If true, we will measure the owner's extents once at BeginPlay and reuse them. The owner's local bounds
	// come from its physics bodies in their current pose, so for an animated owner they change every frame,
	// which would move the points we trace from for reasons that have nothing to do with the ground. Turn this
	// off only if the owner's footprint genuinely changes during play. Only used when bOverrideOwnerExtents
	// is false: an editor tick always measures the owner as it stands, so live edits are reflected there.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="!bOverrideOwnerExtents"))
	bool bCacheOwnerExtents = true;

	// If true, we will draw the rectangle we trace the four corners from, at the owner's origin height.
	// Editor only: it draws in the level editor and Blueprint preview viewports, is never drawn during play
	// (including Play In Editor), and is compiled out of a packaged game entirely.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay)
	bool bDrawDebug = false;

private:
	// Unscaled owner-local extents, set at BeginPlay when bCacheOwnerExtents is true. Never set in the editor.
	TOptional<FVector2D> CachedExtents;
};
