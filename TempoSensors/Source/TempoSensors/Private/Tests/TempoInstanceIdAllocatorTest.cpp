// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorLabeler.h"

#include "Misc/AutomationTest.h"

// Covers FInstanceIdAllocator, which hands out the labeler's instance IDs and takes them back as
// labeled objects go away. Only 253 IDs exist, so a large world shares them between live objects,
// and the allocator has to keep count of every ID's sharing through arbitrary interleavings of
// allocations and returns. Run via Scripts/Test.sh, or from the editor console with
//   Automation RunTests Tempo.Sensors.InstanceIdAllocator

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoInstanceIdAllocatorTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// Allocates Count IDs, or fewer if the allocator refuses first.
	TArray<int32> AllocateMany(FInstanceIdAllocator& Allocator, bool bAllowSharedIds, int32 Count)
	{
		TArray<int32> Ids;
		for (int32 I = 0; I < Count; ++I)
		{
			const TOptional<int32> Id = Allocator.Allocate(bAllowSharedIds);
			if (!Id.IsSet())
			{
				break;
			}
			Ids.Add(*Id);
		}
		return Ids;
	}

	// The IDs in ascending order, for comparing against an expected set of IDs whatever order they
	// were handed out in.
	TArray<int32> Sorted(TArrayView<const int32> Ids)
	{
		TArray<int32> SortedIds(Ids.GetData(), Ids.Num());
		SortedIds.Sort();
		return SortedIds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoInstanceIdAllocatorExclusiveTest,
	"Tempo.Sensors.InstanceIdAllocator.Exclusive", TempoInstanceIdAllocatorTestFlags)
bool FTempoInstanceIdAllocatorExclusiveTest::RunTest(const FString& Parameters)
{
	FInstanceIdAllocator Allocator(1, 3);

	// Every ID goes out once, in some order, and then nothing more while sharing is disallowed.
	const TArray<int32> Ids = AllocateMany(Allocator, false, 4);
	TestEqual(TEXT("IDs handed out before refusing"), Ids.Num(), 3);
	TestEqual(TEXT("Distinct IDs handed out"), Sorted(Ids), TArray<int32>({ 1, 2, 3 }));
	TestFalse(TEXT("Refuses once every ID is held"), Allocator.Allocate(false).IsSet());

	// A returned ID is the one handed out next, and it is the only one available.
	Allocator.Return(2);
	TestEqual(TEXT("Returned ID is handed out again"), Allocator.Allocate(false).Get(-1), 2);
	TestFalse(TEXT("Refuses again once the returned ID is retaken"), Allocator.Allocate(false).IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoInstanceIdAllocatorSharedTest,
	"Tempo.Sensors.InstanceIdAllocator.Shared", TempoInstanceIdAllocatorTestFlags)
bool FTempoInstanceIdAllocatorSharedTest::RunTest(const FString& Parameters)
{
	FInstanceIdAllocator Allocator(1, 3);

	// With sharing allowed, exhausting the range starts a second round over every ID rather than
	// piling onto one: after six allocations each ID is held exactly twice.
	const TArray<int32> Ids = AllocateMany(Allocator, true, 6);
	TestEqual(TEXT("Sharing never refuses"), Ids.Num(), 6);
	TestEqual(TEXT("First round covers every ID"), Sorted(TArrayView<const int32>(Ids).Left(3)), TArray<int32>({ 1, 2, 3 }));
	TestEqual(TEXT("Second round covers every ID"), Sorted(TArrayView<const int32>(Ids).Right(3)), TArray<int32>({ 1, 2, 3 }));

	// Returning one allocation of an ID makes it the least shared, so it goes out next, but it is
	// still held once, so an exclusive allocation is refused.
	Allocator.Return(2);
	TestFalse(TEXT("Refuses an exclusive ID while every ID is held"), Allocator.Allocate(false).IsSet());
	TestEqual(TEXT("Least-shared ID is handed out first"), Allocator.Allocate(true).Get(-1), 2);

	// Returning every allocation of an ID frees it outright.
	Allocator.Return(3);
	Allocator.Return(3);
	TestEqual(TEXT("Fully returned ID is free again"), Allocator.Allocate(false).Get(-1), 3);

	return true;
}

// Tearing down a world returns IDs in whatever order its components unregister, which for a
// shared ID means all of its allocations coming back at once rather than interleaved with other
// IDs'. The allocator has to come back to a clean slate from any such ordering.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoInstanceIdAllocatorTeardownOrderTest,
	"Tempo.Sensors.InstanceIdAllocator.TeardownOrder", TempoInstanceIdAllocatorTestFlags)
bool FTempoInstanceIdAllocatorTeardownOrderTest::RunTest(const FString& Parameters)
{
	FInstanceIdAllocator Allocator(1, 3);

	// Three IDs, five holders: two IDs are shared.
	const TArray<int32> Ids = AllocateMany(Allocator, true, 5);
	TestEqual(TEXT("Sharing never refuses"), Ids.Num(), 5);

	// Return every allocation of one ID before moving to the next.
	TMap<int32, int32> LiveCounts;
	for (const int32 Id : Ids)
	{
		++LiveCounts.FindOrAdd(Id);
	}
	for (const auto& [Id, LiveCount] : LiveCounts)
	{
		for (int32 I = 0; I < LiveCount; ++I)
		{
			Allocator.Return(Id);
		}
	}

	// Everything is free again: every ID goes out exclusively, exactly once.
	const TArray<int32> IdsAfter = AllocateMany(Allocator, false, 4);
	TestEqual(TEXT("IDs handed out exclusively after full teardown"), IdsAfter.Num(), 3);
	TestEqual(TEXT("Distinct IDs after full teardown"), Sorted(IdsAfter), TArray<int32>({ 1, 2, 3 }));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
