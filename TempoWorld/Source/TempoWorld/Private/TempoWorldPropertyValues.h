// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "UObject/UnrealType.h"

#include <grpcpp/support/status.h>

class UWorld;

namespace TempoCore
{
	class Transform;
}

namespace TempoWorld
{
	class Value;
}

// The two halves of the Value API, which are otherwise private to the service subsystem: one reads
// any value-expressible property into a Value, the other writes a Value into one. Declared here so
// a unit test can drive them against a struct of its own without a world or an RPC in between.

// A resolved write destination: one property (with any nested inner path) inside a container.
// Object is the owning UObject when writing to an actor or component, and null when writing into
// a UFunction's parameter frame or a bare struct, which has no owner to notify or invalidate.
struct FPropertyTarget
{
	UObject* Object = nullptr;
	void* Container = nullptr;
	FProperty* Property = nullptr;
	FString InnerPropertyName;
};

// Read a property into a Value, in Unreal's own units. The inverse of SetValueOnTarget, and driven by
// the same classifier, so that a property the generated stubs present as readable is one this can
// actually read.
grpc::Status WritePropertyToValue(const void* Container, const FProperty* Property, TempoWorld::Value& OutValue);

// Write a Value, in Unreal's own units, into Target. The one place that knows how each Value variant
// maps onto Unreal property types; SetProperty, the deprecated Set*Property RPCs and CallFunction's
// arguments all come through here. World is consulted only to resolve an actor or component the
// value names and to announce an edit to an owning object, so it may be null when Target has neither.
grpc::Status SetValueOnTarget(const UWorld* World, const FPropertyTarget& Target, const TempoWorld::Value& Value);

// The deprecated typed Set*Property RPCs take the API's units -- radians, meters, right-handed --
// where a Value is Unreal's own. Each converts its payload into the native Value it means here, at
// its entry point, so the write path above never converts.
TempoWorld::Value RotatorValueFromSI(double Roll, double Pitch, double Yaw);
TempoWorld::Value QuatValueFromSI(double X, double Y, double Z, double W);
// The scale, unitless, is taken as given, and (1, 1, 1) when unset.
TempoWorld::Value TransformValueFromSI(const TempoCore::Transform& Transform);
