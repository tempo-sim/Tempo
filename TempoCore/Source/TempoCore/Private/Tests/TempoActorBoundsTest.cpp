// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

// Unit tests for UTempoCoreUtils::GetActorLocalBounds: only the meshes that are drawn bound an actor.
//
// Builds a throwaway world and an actor carrying the engine's 1 m cube several times over, placed so
// each one's contribution is visible in the result. Runs headlessly under -nullrhi:
//   Scripts/Test.sh Tempo.Core.ActorBounds

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoActorBoundsTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	constexpr double BoundsTol = 1e-3;

	// RAII: a transient Game world with one actor, a scene-component root at the origin, and helpers to
	// add parts to it. Everything is destroyed in the destructor.
	struct FBoundsTestFixture
	{
		UWorld* World = nullptr;
		AActor* Actor = nullptr;
		UStaticMesh* Cube = nullptr;   // the engine's 100 cm cube, centered on its origin

		FBoundsTestFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);

			Actor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();

			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		}

		~FBoundsTestFixture()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}

		UStaticMeshComponent* AddCube(const FVector& Location) const
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetupAttachment(Actor->GetRootComponent());
			Mesh->SetRelativeLocation(Location);
			Mesh->RegisterComponent();
			return Mesh;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoActorBoundsDrawnOnlyTest,
	"Tempo.Core.ActorBounds.DrawnMeshesOnly", TempoActorBoundsTestFlags)
bool FTempoActorBoundsDrawnOnlyTest::RunTest(const FString& Parameters)
{
	FBoundsTestFixture Fixture;
	if (!TestNotNull(TEXT("The engine cube loads"), Fixture.Cube))
	{
		return false;
	}

	// Drawn: the one that should bound the actor.
	Fixture.AddCube(FVector::ZeroVector);

	// Not drawn, each in its own way, and each far enough out that counting it would show.
	Fixture.AddCube(FVector(0.0, 0.0, 500.0))->SetHiddenInGame(true);
	Fixture.AddCube(FVector(0.0, 0.0, -500.0))->SetRenderInMainPass(false);
	Fixture.AddCube(FVector(500.0, 0.0, 0.0))->SetVisibility(false);

	// Drawn, but empty: a mesh component with no mesh has nothing to bound, wherever it sits.
	UStaticMeshComponent* EmptyMesh = NewObject<UStaticMeshComponent>(Fixture.Actor);
	EmptyMesh->SetupAttachment(Fixture.Actor->GetRootComponent());
	EmptyMesh->SetRelativeLocation(FVector(0.0, 0.0, -900.0));
	EmptyMesh->RegisterComponent();

	// Drawn, but not a mesh: a collision shape says nothing about the actor's visible extent.
	UBoxComponent* Box = NewObject<UBoxComponent>(Fixture.Actor);
	Box->SetupAttachment(Fixture.Actor->GetRootComponent());
	Box->SetBoxExtent(FVector(50.0));
	Box->SetRelativeLocation(FVector(-500.0, 0.0, 0.0));
	Box->SetHiddenInGame(false);
	Box->RegisterComponent();

	const FBox Drawn = UTempoCoreUtils::GetActorLocalBounds(Fixture.Actor, /*bIncludeHiddenComponents=*/false);
	TestTrue(TEXT("Drawn bounds are valid"), static_cast<bool>(Drawn.IsValid));
	TestEqual(TEXT("Drawn bounds min"), Drawn.Min, FVector(-50.0), BoundsTol);
	TestEqual(TEXT("Drawn bounds max"), Drawn.Max, FVector(50.0), BoundsTol);

	// Asking for hidden components brings back every mesh, but still not the empty one or the collision
	// shape.
	const FBox All = UTempoCoreUtils::GetActorLocalBounds(Fixture.Actor, /*bIncludeHiddenComponents=*/true);
	TestEqual(TEXT("All-mesh bounds min"), All.Min, FVector(-50.0, -50.0, -550.0), BoundsTol);
	TestEqual(TEXT("All-mesh bounds max"), All.Max, FVector(550.0, 50.0, 550.0), BoundsTol);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoActorBoundsLocalFrameTest,
	"Tempo.Core.ActorBounds.LocalFrame", TempoActorBoundsTestFlags)
bool FTempoActorBoundsLocalFrameTest::RunTest(const FString& Parameters)
{
	FBoundsTestFixture Fixture;
	if (!TestNotNull(TEXT("The engine cube loads"), Fixture.Cube))
	{
		return false;
	}

	// An offset mesh on a moved, turned and scaled actor: the bounds stay in the actor's unscaled local
	// frame, so only the mesh's relative placement shows.
	Fixture.AddCube(FVector(200.0, 0.0, 100.0));
	Fixture.Actor->SetActorTransform(FTransform(FRotator(0.0, 90.0, 0.0), FVector(1000.0, -2000.0, 300.0), FVector(2.0)));

	const FBox Bounds = UTempoCoreUtils::GetActorLocalBounds(Fixture.Actor, /*bIncludeHiddenComponents=*/false);
	TestEqual(TEXT("Local bounds min"), Bounds.Min, FVector(150.0, -50.0, 50.0), BoundsTol);
	TestEqual(TEXT("Local bounds max"), Bounds.Max, FVector(250.0, 50.0, 150.0), BoundsTol);

	// No drawn mesh at all: an invalid box, not a zero-size one at the origin. (A second actor in the same
	// world: a second world at once would register a second set of Tempo services.)
	AActor* Bare = Fixture.World->SpawnActor<AActor>();
	const FBox None = UTempoCoreUtils::GetActorLocalBounds(Bare, /*bIncludeHiddenComponents=*/false);
	TestFalse(TEXT("No drawn mesh gives an invalid box"), static_cast<bool>(None.IsValid));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
