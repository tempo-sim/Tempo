// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGPUFrameThrottle.h"

#include "Misc/AutomationTest.h"

// Pure unit tests for the GPU frame throttle's bookkeeping: which earlier frame a frame must see
// complete before it ends, how the lag is measured, and that the fence ring never overwrites a slot
// still inside the measurement window. No RHI; run via Scripts/Test.sh Tempo.Sensors.GPUFrameThrottle.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// ClientContext too: nothing here needs the editor, so a packaged Development game can run it
	// with -ExecCmds="Automation RunTests Tempo.Sensors.GPUFrameThrottle".
	constexpr EAutomationTestFlags TempoThrottleTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoGPUFrameThrottleWaitFrameTest,
	"Tempo.Sensors.GPUFrameThrottle.FrameToWaitFor", TempoThrottleTestFlags)
bool FTempoGPUFrameThrottleWaitFrameTest::RunTest(const FString& Parameters)
{
	// Nothing to wait on until as many frames as the bound have been issued.
	TestFalse(TEXT("frame 0, bound 1: no wait"), FTempoGPUFrameThrottle::FrameToWaitFor(0, 1).IsSet());
	TestFalse(TEXT("frame 1, bound 2: no wait"), FTempoGPUFrameThrottle::FrameToWaitFor(1, 2).IsSet());

	// Then the frame exactly the bound earlier: with a bound of 1 the previous frame, as a present waits.
	TestEqual(TEXT("frame 1, bound 1 waits for 0"), FTempoGPUFrameThrottle::FrameToWaitFor(1, 1).Get(MAX_uint64), 0ull);
	TestEqual(TEXT("frame 7, bound 1 waits for 6"), FTempoGPUFrameThrottle::FrameToWaitFor(7, 1).Get(MAX_uint64), 6ull);
	TestEqual(TEXT("frame 7, bound 2 waits for 5"), FTempoGPUFrameThrottle::FrameToWaitFor(7, 2).Get(MAX_uint64), 5ull);

	// A bound below one is treated as one rather than as "wait for the frame being ended".
	TestEqual(TEXT("bound 0 behaves as 1"), FTempoGPUFrameThrottle::FrameToWaitFor(3, 0).Get(MAX_uint64), 2ull);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoGPUFrameThrottleMeasureLagTest,
	"Tempo.Sensors.GPUFrameThrottle.MeasureLag", TempoThrottleTestFlags)
bool FTempoGPUFrameThrottleMeasureLagTest::RunTest(const FString& Parameters)
{
	constexpr int32 Window = FTempoGPUFrameThrottle::MeasureWindow;

	// Frames complete in order, so the lag is the run of incomplete frames just before this one.
	const auto CompleteBefore = [](uint64 FirstIncomplete)
	{
		return [FirstIncomplete](uint64 Frame) { return Frame < FirstIncomplete; };
	};

	TestEqual(TEXT("everything complete: lag 0"), FTempoGPUFrameThrottle::MeasureLag(20, Window, CompleteBefore(20)), 0);
	TestEqual(TEXT("previous frame outstanding: lag 1"), FTempoGPUFrameThrottle::MeasureLag(20, Window, CompleteBefore(19)), 1);
	TestEqual(TEXT("three frames outstanding: lag 3"), FTempoGPUFrameThrottle::MeasureLag(20, Window, CompleteBefore(17)), 3);

	// The measurement saturates at the window once there are more incomplete frames than it spans.
	TestEqual(TEXT("saturates at the window"), FTempoGPUFrameThrottle::MeasureLag(Window + 50, Window, CompleteBefore(0)), Window);

	// And never looks before frame 0.
	TestEqual(TEXT("frame 0 has no predecessors"), FTempoGPUFrameThrottle::MeasureLag(0, Window, CompleteBefore(0)), 0);
	TestEqual(TEXT("frame 2 has two predecessors"), FTempoGPUFrameThrottle::MeasureLag(2, Window, CompleteBefore(0)), 2);

	// Completion is checked from the most recent frame backwards and stops at the first complete one,
	// so an older incomplete frame behind a complete one is not reachable (and not a valid state).
	int32 NumQueried = 0;
	const int32 Lag = FTempoGPUFrameThrottle::MeasureLag(20, Window, [&NumQueried](uint64 Frame)
	{
		++NumQueried;
		return Frame == 19;
	});
	TestEqual(TEXT("stops at the first complete frame: lag"), Lag, 0);
	TestEqual(TEXT("stops at the first complete frame: queries"), NumQueried, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoGPUFrameThrottleRingTest,
	"Tempo.Sensors.GPUFrameThrottle.Ring", TempoThrottleTestFlags)
bool FTempoGPUFrameThrottleRingTest::RunTest(const FString& Parameters)
{
	constexpr int32 Window = FTempoGPUFrameThrottle::MeasureWindow;
	constexpr int32 NumSlots = FTempoGPUFrameThrottle::NumSlots();

	// The slot a frame writes must differ from every slot the window still reads: the Window frames
	// before it. Checked across more than one full wrap of the ring.
	for (uint64 Frame = 0; Frame < 4 * NumSlots; ++Frame)
	{
		const int32 WriteSlot = FTempoGPUFrameThrottle::SlotForFrame(Frame);
		TestTrue(TEXT("slot in range"), WriteSlot >= 0 && WriteSlot < NumSlots);
		for (uint64 Back = 1; Back <= static_cast<uint64>(Window) && Back <= Frame; ++Back)
		{
			if (FTempoGPUFrameThrottle::SlotForFrame(Frame - Back) == WriteSlot)
			{
				AddError(FString::Printf(TEXT("frame %llu overwrites the slot of frame %llu, which is still in the window"), Frame, Frame - Back));
				return false;
			}
		}
	}

	// The largest bound the settings allow waits on a frame that is still inside the window.
	const TOptional<uint64> Oldest = FTempoGPUFrameThrottle::FrameToWaitFor(100, Window);
	TestTrue(TEXT("max bound waits inside the window"), Oldest.IsSet() && 100 - Oldest.GetValue() <= static_cast<uint64>(Window));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
