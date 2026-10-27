// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include <grpcpp/support/status.h>

namespace TempoCore
{
	class Transform;
}

namespace TempoWorld
{
	class SetActorTransformRequest;
	class SetComponentTransformRequest;
}

// Conversions between the API's TempoCore.Transform (meters, radians, right-handed, unitless scale)
// and Unreal's FTransform, private to the service subsystem. Declared here so a unit test can drive
// them without a world or an RPC in between.

// An unset scale is (1, 1, 1).
FTransform ToUnrealTransform(const TempoCore::Transform& Transform);

// Always sets the scale.
TempoCore::Transform FromUnrealTransform(const FTransform& Transform);

// The scale a SetActorTransform or SetComponentTransform request asks for, left unset when it asks for
// none: its transform's own scale, or else the deprecated standalone field that scale replaced. A
// request that sets both is INVALID_ARGUMENT.
grpc::Status GetRequestedScale(const TempoWorld::SetActorTransformRequest& Request, TOptional<FVector>& OutScale);
grpc::Status GetRequestedScale(const TempoWorld::SetComponentTransformRequest& Request, TOptional<FVector>& OutScale);
