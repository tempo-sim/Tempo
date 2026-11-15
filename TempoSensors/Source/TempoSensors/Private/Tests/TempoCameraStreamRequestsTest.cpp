// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCameraStreamRequests.h"

#include "TempoCamera.h"

#include "Misc/AutomationTest.h"

// Pure unit tests for which capture may answer a camera's image request. No RHI; run via
// Scripts/Test.sh Tempo.Sensors.CameraStreamRequests.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoStreamRequestsTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	TResponseDelegate<TempoSensors::ColorImage> MakeStream()
	{
		return TResponseDelegate<TempoSensors::ColorImage>::CreateLambda([](const TempoSensors::ColorImage&, grpc::Status) {});
	}

	FColorImageRequest MakeRequest(const TResponseDelegate<TempoSensors::ColorImage>& Stream, const TMap<FDelegateHandle, int32>& StreamLastSequenceIds, int32 NextSequenceId)
	{
		return { TempoSensors::ColorImageRequest(), Stream,
			TempoCameraStreamRequests::GetMinSequenceId(StreamLastSequenceIds, Stream.GetHandle(), NextSequenceId) };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoCameraStreamRequestsFirstRequestTest,
	"Tempo.Sensors.CameraStreamRequests.FirstRequestWaitsForNextCapture", TempoStreamRequestsTestFlags)
bool FTempoCameraStreamRequestsFirstRequestTest::RunTest(const FString& Parameters)
{
	TMap<FDelegateHandle, int32> StreamLastSequenceIds;
	const TResponseDelegate<TempoSensors::ColorImage> Stream = MakeStream();

	// Captures 0 through 4 are taken; the request arrives before capture 5.
	TArray<FColorImageRequest> Pending;
	Pending.Add(MakeRequest(Stream, StreamLastSequenceIds, 5));

	// A read captured before the request arrived answers nobody, and leaves the request pending.
	TestEqual(TEXT("capture 4 answers nobody"), TempoCameraStreamRequests::TakeRequestsAnsweredBy(Pending, 4, StreamLastSequenceIds).Num(), 0);
	TestEqual(TEXT("the request is still pending"), Pending.Num(), 1);
	TestFalse(TEXT("the stream has not been answered"), StreamLastSequenceIds.Contains(Stream.GetHandle()));

	TestEqual(TEXT("capture 5 answers it"), TempoCameraStreamRequests::TakeRequestsAnsweredBy(Pending, 5, StreamLastSequenceIds).Num(), 1);
	TestEqual(TEXT("nothing is left pending"), Pending.Num(), 0);
	TestEqual(TEXT("the stream was last answered with capture 5"), StreamLastSequenceIds.FindRef(Stream.GetHandle()), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoCameraStreamRequestsLaterRequestTest,
	"Tempo.Sensors.CameraStreamRequests.LaterRequestTakesCaptureAlreadyTaken", TempoStreamRequestsTestFlags)
bool FTempoCameraStreamRequestsLaterRequestTest::RunTest(const FString& Parameters)
{
	TMap<FDelegateHandle, int32> StreamLastSequenceIds;
	const TResponseDelegate<TempoSensors::ColorImage> Stream = MakeStream();

	TArray<FColorImageRequest> Pending;
	Pending.Add(MakeRequest(Stream, StreamLastSequenceIds, 5));
	TempoCameraStreamRequests::TakeRequestsAnsweredBy(Pending, 5, StreamLastSequenceIds);

	// The stream asks again once captures 6 and 7 have been taken, as with pipelined rendering. A
	// copy of its delegate is the same stream, so capture 6 answers it.
	const TResponseDelegate<TempoSensors::ColorImage> StreamCopy = Stream;
	Pending.Add(MakeRequest(StreamCopy, StreamLastSequenceIds, 8));
	TestEqual(TEXT("its next request may take capture 6"), Pending[0].MinSequenceId, 6);

	// A stream never seen before, asking at the same moment, waits for capture 8.
	const TResponseDelegate<TempoSensors::ColorImage> NewStream = MakeStream();
	Pending.Add(MakeRequest(NewStream, StreamLastSequenceIds, 8));
	TestEqual(TEXT("a new stream's request waits for capture 8"), Pending[1].MinSequenceId, 8);

	// Never the capture it was last answered with, or an older one.
	TArray<FColorImageRequest> Stale = TempoCameraStreamRequests::TakeRequestsAnsweredBy(Pending, 5, StreamLastSequenceIds);
	TestEqual(TEXT("capture 5 answers nobody a second time"), Stale.Num(), 0);

	TArray<FColorImageRequest> Taken = TempoCameraStreamRequests::TakeRequestsAnsweredBy(Pending, 6, StreamLastSequenceIds);
	TestEqual(TEXT("capture 6 answers one request"), Taken.Num(), 1);
	TestTrue(TEXT("the first stream's"), Taken.Num() == 1 && Taken[0].ResponseContinuation.GetHandle() == Stream.GetHandle());
	TestEqual(TEXT("the new stream's request is still pending"), Pending.Num(), 1);
	TestTrue(TEXT("and it is the new stream's"), Pending.Num() == 1 && Pending[0].ResponseContinuation.GetHandle() == NewStream.GetHandle());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoCameraStreamRequestsOrderTest,
	"Tempo.Sensors.CameraStreamRequests.TakenInOrder", TempoStreamRequestsTestFlags)
bool FTempoCameraStreamRequestsOrderTest::RunTest(const FString& Parameters)
{
	TMap<FDelegateHandle, int32> StreamLastSequenceIds;
	const TResponseDelegate<TempoSensors::ColorImage> StreamA = MakeStream();
	const TResponseDelegate<TempoSensors::ColorImage> StreamB = MakeStream();
	const TResponseDelegate<TempoSensors::ColorImage> StreamC = MakeStream();

	TArray<FColorImageRequest> Pending;
	Pending.Add(MakeRequest(StreamA, StreamLastSequenceIds, 3));
	Pending.Add(MakeRequest(StreamB, StreamLastSequenceIds, 5));
	Pending.Add(MakeRequest(StreamC, StreamLastSequenceIds, 4));

	const TArray<FColorImageRequest> Taken = TempoCameraStreamRequests::TakeRequestsAnsweredBy(Pending, 4, StreamLastSequenceIds);
	TestEqual(TEXT("capture 4 answers two requests"), Taken.Num(), 2);
	TestTrue(TEXT("in the order they arrived"), Taken.Num() == 2
		&& Taken[0].ResponseContinuation.GetHandle() == StreamA.GetHandle()
		&& Taken[1].ResponseContinuation.GetHandle() == StreamC.GetHandle());
	TestTrue(TEXT("and leaves the one that asked for a newer capture"), Pending.Num() == 1
		&& Pending[0].ResponseContinuation.GetHandle() == StreamB.GetHandle());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
