// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoServer.h"
#include "TempoSharedMemory.h"

#include "TempoSensors/Lidar.pb.h"

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

// The server half of the shared-memory transport (TempoSharedMemory.h), read back here the way a client
// reads it. No RHI; run via Scripts/Test.sh Tempo.Sensors.SharedMemory.

#if WITH_DEV_AUTOMATION_TESTS && (PLATFORM_LINUX || PLATFORM_MAC)

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace
{
	constexpr EAutomationTestFlags TempoSharedMemoryTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	template <typename T>
	T Read(const uint8* Base, uint64 Offset)
	{
		T Value;
		FMemory::Memcpy(&Value, Base + Offset, sizeof(T));
		return Value;
	}

	struct FRef
	{
		uint32 Slot = 0;
		uint64 Sequence = 0;
		uint64 Offset = 0;
		uint64 Length = 0;
		uint64 SegmentSize = 0;
		FString Name;
	};

	bool ParseRef(const std::string& Field, FRef& Ref)
	{
		if (Field.size() != FTempoSharedMemoryStream::RefSize || Field.compare(0, 8, "TEMPOREF") != 0)
		{
			return false;
		}
		const uint8* Data = reinterpret_cast<const uint8*>(Field.data());
		Ref.Slot = Read<uint32>(Data, 12);
		Ref.Sequence = Read<uint64>(Data, 16);
		Ref.Offset = Read<uint64>(Data, 24);
		Ref.Length = Read<uint64>(Data, 32);
		Ref.SegmentSize = Read<uint64>(Data, 40);
		Ref.Name = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Data + 48)));
		return true;
	}

	// A client's mapping of one segment.
	struct FClientMapping
	{
		uint8* Base = nullptr;
		uint64 Size = 0;

		explicit FClientMapping(const FRef& Ref)
		{
			const int Fd = shm_open(TCHAR_TO_UTF8(*Ref.Name), O_RDWR, 0);
			if (Fd >= 0)
			{
				void* Mapped = mmap(nullptr, Ref.SegmentSize, PROT_READ | PROT_WRITE, MAP_SHARED, Fd, 0);
				close(Fd);
				if (Mapped != MAP_FAILED)
				{
					Base = static_cast<uint8*>(Mapped);
					Size = Ref.SegmentSize;
					FPlatformAtomics::AtomicStore(reinterpret_cast<volatile int32*>(Base + 24), 1);
				}
			}
		}

		~FClientMapping()
		{
			if (Base)
			{
				munmap(Base, Size);
			}
		}

		std::string Payload(const FRef& Ref) const
		{
			return std::string(reinterpret_cast<const char*>(Base + Ref.Offset), Ref.Length);
		}

		int32 SlotState(uint32 Slot) const
		{
			return FPlatformAtomics::AtomicRead(reinterpret_cast<volatile const int32*>(Base + 64 + Slot * 16));
		}

		uint64 SlotSequence(uint32 Slot) const
		{
			return Read<uint64>(Base, 64 + Slot * 16 + 8);
		}

		void Free(uint32 Slot) const
		{
			FPlatformAtomics::AtomicStore(reinterpret_cast<volatile int32*>(Base + 64 + Slot * 16), 0);
		}
	};

	TempoSensors::LidarScanSegment MakeScan(int32 Returns, uint8 Fill)
	{
		TempoSensors::LidarScanSegment Scan;
		Scan.mutable_header()->set_sequence_id(42);
		Scan.mutable_header()->set_sensor("Lidar");
		Scan.set_vertical_beams(Returns / 64);
		Scan.set_horizontal_beams(64);
		Scan.mutable_azimuth_range_rad()->set_min(-1.f);
		Scan.set_return_mode(TempoSensors::LRM_DUAL);
		Scan.set_distances_m(std::string(Returns * 4, static_cast<char>(Fill)));
		Scan.set_intensities(std::string(Returns * 4, static_cast<char>(Fill + 1)));
		Scan.set_reflectivities(std::string(16, static_cast<char>(Fill + 2))); // Small: stays inline.
		return Scan;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoSharedMemoryRoundTripTest, "Tempo.Sensors.SharedMemory.RoundTrip", TempoSharedMemoryTestFlags)
bool FTempoSharedMemoryRoundTripTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FTempoSharedMemoryStream> Stream = FTempoSharedMemoryStream::Create(TempoSensors::LidarScanSegment::descriptor());
	if (!TestNotNull(TEXT("a lidar stream has a shared-memory channel"), Stream.Get()))
	{
		return false;
	}
	const TempoSensors::LidarScanSegment Scan = MakeScan(64 * 1024, 7);
	TempoSensors::LidarScanSegment Envelope;
	if (!TestTrue(TEXT("the scan is encoded"), Stream->Encode(Scan, Envelope)))
	{
		return false;
	}

	FRef Distances, Intensities;
	TestTrue(TEXT("distances are a reference"), ParseRef(Envelope.distances_m(), Distances));
	TestTrue(TEXT("intensities are a reference"), ParseRef(Envelope.intensities(), Intensities));
	TestEqual(TEXT("both share a slot"), Distances.Slot, Intensities.Slot);
	TestEqual(TEXT("a small field stays inline"), Envelope.reflectivities(), Scan.reflectivities());

	const FClientMapping Mapping(Distances);
	if (!TestNotNull(TEXT("the client maps the segment"), Mapping.Base))
	{
		return false;
	}
	TestEqual(TEXT("the slot is written"), Mapping.SlotState(Distances.Slot), 1);
	TestEqual(TEXT("the slot holds this message"), Mapping.SlotSequence(Distances.Slot), Distances.Sequence);

	TempoSensors::LidarScanSegment Restored = Envelope;
	Restored.set_distances_m(Mapping.Payload(Distances));
	Restored.set_intensities(Mapping.Payload(Intensities));
	TestTrue(TEXT("the restored message is the original"), Restored.SerializeAsString() == Scan.SerializeAsString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoSharedMemoryRingFullTest, "Tempo.Sensors.SharedMemory.RingFullGoesInline", TempoSharedMemoryTestFlags)
bool FTempoSharedMemoryRingFullTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Slots = IConsoleManager::Get().FindConsoleVariable(TEXT("Tempo.SharedMemory.Slots"));
	const int32 PreviousSlots = Slots->GetInt();
	Slots->Set(2, ECVF_SetByCode);
	ON_SCOPE_EXIT { Slots->Set(PreviousSlots, ECVF_SetByCode); };

	TUniquePtr<FTempoSharedMemoryStream> Stream = FTempoSharedMemoryStream::Create(TempoSensors::LidarScanSegment::descriptor());
	const TempoSensors::LidarScanSegment Scan = MakeScan(64 * 1024, 1);
	TempoSensors::LidarScanSegment First, Second, Third;
	TestTrue(TEXT("the first message takes a slot"), Stream->Encode(Scan, First));
	TestTrue(TEXT("the second takes the other"), Stream->Encode(Scan, Second));
	TestFalse(TEXT("with no slot free, the third goes inline"), Stream->Encode(Scan, Third));

	FRef Ref;
	ParseRef(First.distances_m(), Ref);
	const FClientMapping Mapping(Ref);
	Mapping.Free(Ref.Slot);
	TestTrue(TEXT("once the client frees one, the next takes it"), Stream->Encode(Scan, Third));
	FRef ThirdRef;
	ParseRef(Third.distances_m(), ThirdRef);
	TestEqual(TEXT("in the slot freed"), ThirdRef.Slot, Ref.Slot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoSharedMemoryResizeTest, "Tempo.Sensors.SharedMemory.LargerMessageMovesSegment", TempoSharedMemoryTestFlags)
bool FTempoSharedMemoryResizeTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FTempoSharedMemoryStream> Stream = FTempoSharedMemoryStream::Create(TempoSensors::LidarScanSegment::descriptor());
	TempoSensors::LidarScanSegment Small, Large;
	TestTrue(TEXT("a small scan is encoded"), Stream->Encode(MakeScan(64 * 1024, 1), Small));
	const TempoSensors::LidarScanSegment LargeScan = MakeScan(256 * 1024, 2);
	TestTrue(TEXT("a larger one is encoded"), Stream->Encode(LargeScan, Large));

	FRef SmallRef, LargeRef;
	ParseRef(Small.distances_m(), SmallRef);
	ParseRef(Large.distances_m(), LargeRef);
	TestNotEqual(TEXT("the larger one is in a new segment"), LargeRef.Name, SmallRef.Name);

	// The client has not read the first segment yet, so it must still be there to map.
	const FClientMapping SmallMapping(SmallRef);
	TestNotNull(TEXT("the earlier segment can still be mapped"), SmallMapping.Base);
	const FClientMapping LargeMapping(LargeRef);
	if (TestNotNull(TEXT("the new segment can be mapped"), LargeMapping.Base))
	{
		TestTrue(TEXT("and holds the larger payload"), LargeMapping.Payload(LargeRef) == LargeScan.distances_m());
	}

	// Once the client has attached, the server unlinks the name; nothing is left behind.
	TempoSensors::LidarScanSegment Next;
	Stream->Encode(LargeScan, Next);
	TestTrue(TEXT("an attached segment's name is gone"), shm_open(TCHAR_TO_UTF8(*LargeRef.Name), O_RDWR, 0) < 0);
	Stream->Close();
	TestTrue(TEXT("Close removes the rest"), shm_open(TCHAR_TO_UTF8(*SmallRef.Name), O_RDWR, 0) < 0);
	return true;
}

#endif
