// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoClassUtils.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

// Tests for class resolution by path, which is how the reflected API addresses a class: a class
// name is ambiguous (two Blueprints in different folders can generate the same name) while a path is
// not. Resolution by name keeps its documented first-match behavior and is not retested here.
//
//   Scripts/Test.sh
//   Automation RunTests Tempo.Core.ClassUtils

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoClassUtilsTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoClassUtilsPathTest,
	"Tempo.Core.ClassUtils.Path", TempoClassUtilsTestFlags)
bool FTempoClassUtilsPathTest::RunTest(const FString& Parameters)
{
	// A native class resolves by its own object path.
	TestEqual(TEXT("native class by path"),
		GetSubClassWithPath<UActorComponent>(TEXT("/Script/Engine.StaticMeshComponent")),
		UStaticMeshComponent::StaticClass());

	// The base class is enforced: the same path is not an actor.
	TestNull(TEXT("component path is not an actor"),
		GetSubClassWithPath<AActor>(TEXT("/Script/Engine.StaticMeshComponent")));

	// Anything that does not resolve is nullptr rather than an error.
	TestNull(TEXT("unknown object in a real package"),
		GetSubClassWithPath<UObject>(TEXT("/Script/Engine.NoSuchClassExistsHere")));
	TestNull(TEXT("unknown package"),
		GetSubClassWithPath<UObject>(TEXT("/Script/NoSuchModule.Whatever")));
	TestNull(TEXT("package with no object name"),
		GetSubClassWithPath<UObject>(TEXT("/Script/Engine")));

	// A name is not a path, and the path resolver does not guess.
	TestNull(TEXT("bare name is rejected"), GetSubClassWithPath<UObject>(TEXT("StaticMeshComponent")));
	TestNull(TEXT("empty string"), GetSubClassWithPath<UObject>(FString()));

	// GetSubClassWithName routes a leading '/' to the path resolver, so existing callers that are
	// handed a path get the exact class rather than no class at all. A class name can never contain
	// a '/', so this cannot shadow a name.
	TestEqual(TEXT("name resolver accepts a path"),
		GetSubClassWithName<UActorComponent>(TEXT("/Script/Engine.StaticMeshComponent")),
		UStaticMeshComponent::StaticClass());

	// And a name still resolves as a name.
	TestEqual(TEXT("name resolver still accepts a name"),
		GetSubClassWithName<UActorComponent>(TEXT("StaticMeshComponent")),
		UStaticMeshComponent::StaticClass());

	return true;
}

#endif
