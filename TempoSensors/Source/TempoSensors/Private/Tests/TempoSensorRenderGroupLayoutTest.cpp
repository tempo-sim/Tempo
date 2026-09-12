// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorRenderGroup.h"

#include "Misc/AutomationTest.h"

// Pure unit tests for the render group's atlas packer. No world, no RHI. Run via Scripts/Test.sh,
// or from the editor console with
//   Automation RunTests Tempo.Sensors.RenderGroupLayout

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoLayoutTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	using TempoSensorRenderGroupLayout::FBlock;

	bool Overlaps(const FBlock& A, const FBlock& B)
	{
		return A.Offset.X < B.Offset.X + B.Size.X && B.Offset.X < A.Offset.X + A.Size.X
			&& A.Offset.Y < B.Offset.Y + B.Size.Y && B.Offset.Y < A.Offset.Y + A.Size.Y;
	}

	// Every placed block lies inside the atlas and overlaps no other placed block.
	bool CheckPlacement(FAutomationTestBase& Test, TArrayView<const FBlock> Blocks, const FIntPoint& AtlasSize, int32 MaxDimension)
	{
		bool bOk = true;
		bOk &= Test.TestTrue(TEXT("Atlas width within max"), AtlasSize.X <= MaxDimension);
		bOk &= Test.TestTrue(TEXT("Atlas height within max"), AtlasSize.Y <= MaxDimension);
		for (int32 I = 0; I < Blocks.Num(); ++I)
		{
			const FBlock& A = Blocks[I];
			if (A.Offset.X < 0)
			{
				continue;
			}
			bOk &= Test.TestTrue(TEXT("Block inside atlas"),
				A.Offset.X >= 0 && A.Offset.Y >= 0
				&& A.Offset.X + A.Size.X <= AtlasSize.X && A.Offset.Y + A.Size.Y <= AtlasSize.Y);
			for (int32 J = I + 1; J < Blocks.Num(); ++J)
			{
				const FBlock& B = Blocks[J];
				if (B.Offset.X < 0)
				{
					continue;
				}
				bOk &= Test.TestFalse(*FString::Printf(TEXT("Blocks %d and %d overlap"), I, J), Overlaps(A, B));
			}
		}
		return bOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoRenderGroupLayoutSingleTest,
	"Tempo.Sensors.RenderGroupLayout.Single", TempoLayoutTestFlags)
bool FTempoRenderGroupLayoutSingleTest::RunTest(const FString& Parameters)
{
	TArray<FBlock> Blocks = { { FIntPoint(960, 540) } };
	const FIntPoint AtlasSize = TempoSensorRenderGroupLayout::Pack(Blocks, 16384);
	TestEqual(TEXT("Single block atlas is the block"), AtlasSize, FIntPoint(960, 540));
	TestEqual(TEXT("Single block at origin"), Blocks[0].Offset, FIntPoint::ZeroValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoRenderGroupLayoutUniformTest,
	"Tempo.Sensors.RenderGroupLayout.Uniform", TempoLayoutTestFlags)
bool FTempoRenderGroupLayoutUniformTest::RunTest(const FString& Parameters)
{
	// Four identical 540p cameras pack into a 2x2 grid, not a 4x1 strip.
	TArray<FBlock> Blocks;
	for (int32 I = 0; I < 4; ++I)
	{
		Blocks.Add({ FIntPoint(960, 540) });
	}
	const FIntPoint AtlasSize = TempoSensorRenderGroupLayout::Pack(Blocks, 16384);
	CheckPlacement(*this, Blocks, AtlasSize, 16384);
	TestEqual(TEXT("2x2 grid"), AtlasSize, FIntPoint(1920, 1080));
	for (const FBlock& Block : Blocks)
	{
		TestTrue(TEXT("Every block placed"), Block.Offset.X >= 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoRenderGroupLayoutMixedTest,
	"Tempo.Sensors.RenderGroupLayout.Mixed", TempoLayoutTestFlags)
bool FTempoRenderGroupLayoutMixedTest::RunTest(const FString& Parameters)
{
	// Mixed sizes in arbitrary order, including a zero-size block that must be skipped.
	TArray<FBlock> Blocks = {
		{ FIntPoint(640, 480) },
		{ FIntPoint(1920, 1080) },
		{ FIntPoint(0, 0) },
		{ FIntPoint(256, 2048) },
		{ FIntPoint(1280, 720) },
		{ FIntPoint(100, 100) },
	};
	const FIntPoint AtlasSize = TempoSensorRenderGroupLayout::Pack(Blocks, 16384);
	CheckPlacement(*this, Blocks, AtlasSize, 16384);
	TestEqual(TEXT("Zero-size block unplaced"), Blocks[2].Offset, FIntPoint(-1, -1));
	for (int32 I = 0; I < Blocks.Num(); ++I)
	{
		if (I != 2)
		{
			TestTrue(*FString::Printf(TEXT("Block %d placed"), I), Blocks[I].Offset.X >= 0);
		}
	}
	// Deterministic: packing the same input again yields the same placement.
	TArray<FBlock> Again = Blocks;
	for (FBlock& Block : Again)
	{
		Block.Offset = FIntPoint(7, 7);
	}
	const FIntPoint AgainSize = TempoSensorRenderGroupLayout::Pack(Again, 16384);
	TestEqual(TEXT("Deterministic atlas size"), AgainSize, AtlasSize);
	for (int32 I = 0; I < Blocks.Num(); ++I)
	{
		TestEqual(*FString::Printf(TEXT("Deterministic offset %d"), I), Again[I].Offset, Blocks[I].Offset);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoRenderGroupLayoutOverflowTest,
	"Tempo.Sensors.RenderGroupLayout.Overflow", TempoLayoutTestFlags)
bool FTempoRenderGroupLayoutOverflowTest::RunTest(const FString& Parameters)
{
	// A block wider than the max dimension is left unplaced; the rest still pack, within the max.
	TArray<FBlock> Blocks = {
		{ FIntPoint(1000, 1000) },
		{ FIntPoint(3000, 100) },
		{ FIntPoint(1000, 1000) },
		{ FIntPoint(1000, 1000) },
		{ FIntPoint(1000, 1000) },
		{ FIntPoint(1000, 1000) },
	};
	const int32 MaxDimension = 2048;
	const FIntPoint AtlasSize = TempoSensorRenderGroupLayout::Pack(Blocks, MaxDimension);
	CheckPlacement(*this, Blocks, AtlasSize, MaxDimension);
	TestEqual(TEXT("Oversized block unplaced"), Blocks[1].Offset, FIntPoint(-1, -1));
	int32 NumPlaced = 0;
	for (const FBlock& Block : Blocks)
	{
		NumPlaced += Block.Offset.X >= 0 ? 1 : 0;
	}
	// Only four 1000x1000 blocks fit in 2048x2048.
	TestEqual(TEXT("Four of five square blocks placed"), NumPlaced, 4);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
