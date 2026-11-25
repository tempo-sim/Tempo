// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSharedMemory.h"

#include "TempoCore.h"
#include "TempoServer.h"
#include "TempoWriteStats.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformProcess.h"

#include <string>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/message.h"

#define TEMPO_POSIX_SHARED_MEMORY (PLATFORM_LINUX || PLATFORM_MAC)
#define TEMPO_HAS_SHARED_MEMORY (TEMPO_POSIX_SHARED_MEMORY || PLATFORM_WINDOWS)

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#endif

#if TEMPO_POSIX_SHARED_MEMORY
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
	TAutoConsoleVariable<int32> CVarSharedMemory(
		TEXT("Tempo.SharedMemory"), 1,
		TEXT("1 offers shared memory to streaming clients on this machine that ask for it, for their large bytes fields. ")
		TEXT("Applies to streams opened after it changes."));

	TAutoConsoleVariable<int32> CVarSharedMemoryMinBytes(
		TEXT("Tempo.SharedMemory.MinBytes"), 64 * 1024,
		TEXT("The smallest bytes field a shared-memory stream moves into shared memory; smaller ones go inline."));

	TAutoConsoleVariable<int32> CVarSharedMemorySlots(
		TEXT("Tempo.SharedMemory.Slots"), 3,
		TEXT("Slots in each shared-memory stream's ring: how many of its messages a client may hold unread before ")
		TEXT("the next goes inline. Applies to segments made after it changes."));

	constexpr uint64 SegmentMagic = 0x4d48534f504d4554ull; // "TEMPOSHM"
	constexpr uint64 RefMagic = 0x4645524f504d4554ull;     // "TEMPOREF"
	constexpr uint32 LayoutVersion = 1;
	constexpr uint64 HeaderSize = 64;
	constexpr uint64 SlotEntrySize = 16;
	constexpr uint64 PayloadAlignment = 64;
	constexpr uint64 SlotSizeGranularity = 64 * 1024;
	constexpr int32 MaxNameLength = 32;

	constexpr uint64 AlignUp(uint64 Value, uint64 Alignment)
	{
		return (Value + Alignment - 1) / Alignment * Alignment;
	}

	template <typename T>
	void Put(uint8* Base, uint64 Offset, T Value)
	{
		FMemory::Memcpy(Base + Offset, &Value, sizeof(T));
	}

	bool IsLocalPeer(const std::string& Peer)
	{
		static const char* const LocalPrefixes[] = {
			"unix:", "ipv4:127.", "ipv6:[::1]", "ipv6:%5B::1%5D", "ipv6:[::ffff:127.", "ipv6:%5B::ffff:127.",
		};
		for (const char* Prefix : LocalPrefixes)
		{
			if (Peer.rfind(Prefix, 0) == 0)
			{
				return true;
			}
		}
		return false;
	}

	using FMessage = google::protobuf_tempo::Message;
	using FFieldDescriptor = google::protobuf_tempo::FieldDescriptor;
	using FReflection = google::protobuf_tempo::Reflection;

	// FieldDescriptor::type() and cpp_type() resolve a lazily built type under an absl call_once that the
	// shared gRPC library does not export, so a field's kind is read once, from its descriptor proto.
	int32 CppTypeOf(const FFieldDescriptor* Field)
	{
		google::protobuf_tempo::FieldDescriptorProto Proto;
		Field->CopyTo(&Proto);
		switch (Proto.type())
		{
		case 5: case 15: case 17: return FFieldDescriptor::CPPTYPE_INT32;
		case 3: case 16: case 18: return FFieldDescriptor::CPPTYPE_INT64;
		case 13: case 7: return FFieldDescriptor::CPPTYPE_UINT32;
		case 4: case 6: return FFieldDescriptor::CPPTYPE_UINT64;
		case 1: return FFieldDescriptor::CPPTYPE_DOUBLE;
		case 2: return FFieldDescriptor::CPPTYPE_FLOAT;
		case 8: return FFieldDescriptor::CPPTYPE_BOOL;
		case 9: case 12: return FFieldDescriptor::CPPTYPE_STRING;
		case 10: case 11: return FFieldDescriptor::CPPTYPE_MESSAGE;
		case 14: return FFieldDescriptor::CPPTYPE_ENUM;
		default: return 0;
		}
	}

	bool IsSingularBytes(const FFieldDescriptor* Field)
	{
		google::protobuf_tempo::FieldDescriptorProto Proto;
		Field->CopyTo(&Proto);
		// Not a oneof member (nor proto3 `optional`, a oneof of one): every client must be able to reach the
		// field without knowing the message, and Rust's prost folds a oneof into an enum.
		return Proto.type() == google::protobuf_tempo::FieldDescriptorProto::TYPE_BYTES
			&& Proto.label() != google::protobuf_tempo::FieldDescriptorProto::LABEL_REPEATED
			&& !Proto.has_oneof_index();
	}

	// Copies one set field of From into To, which is the same type.
	void CopyField(const FReflection* Reflection, const FMessage& From, FMessage* To, const FFieldDescriptor* Field, int32 CppType)
	{
		if (Field->is_repeated())
		{
			const int Count = Reflection->FieldSize(From, Field);
			for (int Index = 0; Index < Count; ++Index)
			{
				switch (CppType)
				{
				case FFieldDescriptor::CPPTYPE_INT32: Reflection->AddInt32(To, Field, Reflection->GetRepeatedInt32(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_INT64: Reflection->AddInt64(To, Field, Reflection->GetRepeatedInt64(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_UINT32: Reflection->AddUInt32(To, Field, Reflection->GetRepeatedUInt32(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_UINT64: Reflection->AddUInt64(To, Field, Reflection->GetRepeatedUInt64(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_DOUBLE: Reflection->AddDouble(To, Field, Reflection->GetRepeatedDouble(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_FLOAT: Reflection->AddFloat(To, Field, Reflection->GetRepeatedFloat(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_BOOL: Reflection->AddBool(To, Field, Reflection->GetRepeatedBool(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_ENUM: Reflection->AddEnumValue(To, Field, Reflection->GetRepeatedEnumValue(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_STRING: Reflection->AddString(To, Field, Reflection->GetRepeatedString(From, Field, Index)); break;
				case FFieldDescriptor::CPPTYPE_MESSAGE: Reflection->AddMessage(To, Field)->CopyFrom(Reflection->GetRepeatedMessage(From, Field, Index)); break;
				}
			}
			return;
		}
		switch (CppType)
		{
		case FFieldDescriptor::CPPTYPE_INT32: Reflection->SetInt32(To, Field, Reflection->GetInt32(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_INT64: Reflection->SetInt64(To, Field, Reflection->GetInt64(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_UINT32: Reflection->SetUInt32(To, Field, Reflection->GetUInt32(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_UINT64: Reflection->SetUInt64(To, Field, Reflection->GetUInt64(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_DOUBLE: Reflection->SetDouble(To, Field, Reflection->GetDouble(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_FLOAT: Reflection->SetFloat(To, Field, Reflection->GetFloat(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_BOOL: Reflection->SetBool(To, Field, Reflection->GetBool(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_ENUM: Reflection->SetEnumValue(To, Field, Reflection->GetEnumValue(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_STRING: Reflection->SetString(To, Field, Reflection->GetString(From, Field)); break;
		case FFieldDescriptor::CPPTYPE_MESSAGE: Reflection->MutableMessage(To, Field)->CopyFrom(Reflection->GetMessage(From, Field)); break;
		}
	}
}

struct FTempoSharedMemoryStream::FSegment
{
	ANSICHAR Name[MaxNameLength] = {};
	uint8* Base = nullptr;
	uint64 Size = 0;
	uint32 SlotCount = 0;
	uint64 SlotSize = 0;
	uint64 DataOffset = 0;
	bool bUnlinked = false;
#if PLATFORM_WINDOWS
	// The file mapping's name lasts as long as a handle to it does; the view keeps the memory.
	HANDLE Mapping = nullptr;
#endif

	bool IsAttached() const
	{
		return FPlatformAtomics::AtomicRead(reinterpret_cast<volatile const int32*>(Base + 24)) != 0;
	}

	volatile int32* SlotState(uint32 Slot) const
	{
		return reinterpret_cast<volatile int32*>(Base + HeaderSize + Slot * SlotEntrySize);
	}

	uint8* SlotData(uint32 Slot) const
	{
		return Base + DataOffset + Slot * SlotSize;
	}

	// Removes the segment's name, so nothing else can open it. Mappings already made stay valid.
	void Unlink()
	{
		if (bUnlinked)
		{
			return;
		}
		bUnlinked = true;
#if TEMPO_POSIX_SHARED_MEMORY
		shm_unlink(Name);
#elif PLATFORM_WINDOWS
		// The name goes once the client, which closes its handle as soon as it has mapped the view, has too.
		if (Mapping)
		{
			CloseHandle(Mapping);
			Mapping = nullptr;
		}
#endif
	}

	~FSegment()
	{
		Unlink();
		if (Base)
		{
#if TEMPO_POSIX_SHARED_MEMORY
			munmap(Base, Size);
#elif PLATFORM_WINDOWS
			UnmapViewOfFile(Base);
#endif
		}
	}

	static TUniquePtr<FSegment> Make(uint32 SlotCount, uint64 SlotSize)
	{
#if TEMPO_HAS_SHARED_MEMORY
		static std::atomic<uint32> Counter{0};
		TUniquePtr<FSegment> Segment = MakeUnique<FSegment>();
		Segment->SlotCount = SlotCount;
		Segment->SlotSize = SlotSize;
		Segment->DataOffset = AlignUp(HeaderSize + SlotCount * SlotEntrySize, 4096);
		Segment->Size = Segment->DataOffset + SlotCount * SlotSize;
		if (!Segment->Create(Counter))
		{
			return nullptr; // Unlinks it.
		}
		Put<uint64>(Segment->Base, 0, SegmentMagic);
		Put<uint32>(Segment->Base, 8, LayoutVersion);
		Put<uint32>(Segment->Base, 12, SlotCount);
		Put<uint64>(Segment->Base, 16, SlotSize);
		Put<uint64>(Segment->Base, 32, Segment->DataOffset);
		return Segment;
#else
		return nullptr;
#endif
	}

private:
	// At most 31 characters, as macOS requires, the prefix included.
	void NameSegment(std::atomic<uint32>& Counter, const ANSICHAR* Prefix)
	{
		FCStringAnsi::Snprintf(Name, MaxNameLength, "%s%x.%x%04x", Prefix,
			static_cast<uint32>(FPlatformProcess::GetCurrentProcessId()), Counter.fetch_add(1),
			static_cast<uint32>(FMath::Rand() & 0xffff));
	}

	// Creates and maps a segment of Size bytes, zeroed, readable and writable only by this user.
	bool Create(std::atomic<uint32>& Counter)
	{
#if TEMPO_POSIX_SHARED_MEMORY
		int Fd = -1;
		for (int32 Attempt = 0; Attempt < 8 && Fd < 0; ++Attempt)
		{
			NameSegment(Counter, "/tempo.");
			Fd = shm_open(Name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
			if (Fd < 0 && errno != EEXIST)
			{
				break;
			}
		}
		if (Fd < 0)
		{
			UE_LOG(LogTempoCore, Warning, TEXT("Could not create a shared-memory segment: %hs. Streams go inline."), strerror(errno));
			bUnlinked = true;
			return false;
		}

		bool bSized = ftruncate(Fd, static_cast<off_t>(Size)) == 0;
#if PLATFORM_LINUX
		// Reserve the pages now. /dev/shm may be smaller than the segment (a container's is 64 MB by default),
		// and a write to a page tmpfs cannot supply raises SIGBUS rather than failing.
		bSized = bSized && posix_fallocate(Fd, 0, static_cast<off_t>(Size)) == 0;
#endif
		void* Mapped = bSized ? mmap(nullptr, Size, PROT_READ | PROT_WRITE, MAP_SHARED, Fd, 0) : MAP_FAILED;
		close(Fd);
		if (Mapped == MAP_FAILED)
		{
			UE_LOG(LogTempoCore, Warning, TEXT("Could not size a %.1f MB shared-memory segment (is /dev/shm full?). Streams go inline."),
				Size / 1.e6);
			return false;
		}
		Base = static_cast<uint8*>(Mapped);
		return true;
#elif PLATFORM_WINDOWS
		// Backed by the paging file and committed whole, so running out fails here rather than on a write.
		// "Local\" keeps it in this session; the default security descriptor admits only this user (and
		// SYSTEM and administrators).
		for (int32 Attempt = 0; Attempt < 8 && !Mapping; ++Attempt)
		{
			NameSegment(Counter, "Local\\t.");
			Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
				static_cast<DWORD>(Size >> 32), static_cast<DWORD>(Size & 0xffffffffull), Name);
			if (Mapping && GetLastError() == ERROR_ALREADY_EXISTS)
			{
				CloseHandle(Mapping);
				Mapping = nullptr;
			}
			else if (!Mapping)
			{
				break;
			}
		}
		if (!Mapping)
		{
			UE_LOG(LogTempoCore, Warning, TEXT("Could not create a %.1f MB shared-memory segment (error %u). Streams go inline."),
				Size / 1.e6, static_cast<uint32>(GetLastError()));
			bUnlinked = true;
			return false;
		}
		Base = static_cast<uint8*>(MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(Size)));
		if (!Base)
		{
			UE_LOG(LogTempoCore, Warning, TEXT("Could not map a %.1f MB shared-memory segment (error %u). Streams go inline."),
				Size / 1.e6, static_cast<uint32>(GetLastError()));
			return false;
		}
		return true;
#else
		return false;
#endif
	}
};

bool FTempoSharedMemoryStream::IsRequested(const grpc::ServerContext& Context)
{
#if TEMPO_HAS_SHARED_MEMORY
	if (CVarSharedMemory.GetValueOnAnyThread() == 0)
	{
		return false;
	}
	const auto& Metadata = Context.client_metadata();
	const auto Found = Metadata.find("tempo-shm");
	if (Found == Metadata.end() || Found->second != "1")
	{
		return false;
	}
	return IsLocalPeer(Context.peer());
#else
	return false;
#endif
}

TUniquePtr<FTempoSharedMemoryStream> FTempoSharedMemoryStream::Create(const google::protobuf_tempo::Descriptor* Descriptor)
{
#if TEMPO_HAS_SHARED_MEMORY
	TArray<const FFieldDescriptor*> Fields;
	TMap<const FFieldDescriptor*, int32> CppTypes;
	for (int Index = 0; Index < Descriptor->field_count(); ++Index)
	{
		const FFieldDescriptor* Field = Descriptor->field(Index);
		CppTypes.Add(Field, CppTypeOf(Field));
		if (IsSingularBytes(Field))
		{
			Fields.Add(Field);
		}
	}
	if (!Fields.IsEmpty())
	{
		return TUniquePtr<FTempoSharedMemoryStream>(new FTempoSharedMemoryStream(MoveTemp(Fields), MoveTemp(CppTypes)));
	}
#endif
	return nullptr;
}

FTempoSharedMemoryStream::FTempoSharedMemoryStream(TArray<const google::protobuf_tempo::FieldDescriptor*>&& BytesFieldsIn,
	TMap<const google::protobuf_tempo::FieldDescriptor*, int32>&& CppTypesIn)
	: BytesFields(MoveTemp(BytesFieldsIn)), CppTypes(MoveTemp(CppTypesIn))
{
}

FTempoSharedMemoryStream::~FTempoSharedMemoryStream() = default;

void FTempoSharedMemoryStream::Close()
{
	bClosed = true;
	Current.Reset();
	Retired.Reset();
}

void FTempoSharedMemoryStream::Announce(grpc::ServerContext& Context)
{
	if (bAnnounced)
	{
		return;
	}
	bAnnounced = true;
	std::string Name;
	uint64 Size;
	if (GetFirstSegment(Name, Size))
	{
		Context.AddInitialMetadata("tempo-shm-segment", Name);
		Context.AddInitialMetadata("tempo-shm-size", std::to_string(Size));
	}
}

bool FTempoSharedMemoryStream::GetFirstSegment(std::string& Name, uint64& Size) const
{
	if (!Current || bClosed || bClientAttached || Current->IsAttached())
	{
		return false;
	}
	Name = Current->Name;
	Size = Current->Size;
	return true;
}

void FTempoSharedMemoryStream::ReleaseAttachedSegments()
{
	if (Current && Current->IsAttached())
	{
		bClientAttached = true;
		if (!Current->bUnlinked)
		{
			Current->Unlink();
		}
	}
	// The client maps a segment the first time a reference names it, so once it has attached it holds its own
	// mapping and needs neither the name nor ours.
	if (Retired.RemoveAll([](const TUniquePtr<FSegment>& Segment) { return Segment->IsAttached(); }) > 0)
	{
		bClientAttached = true;
	}
}

FTempoSharedMemoryStream::FSegment* FTempoSharedMemoryStream::EnsureSegment(uint64 SlotSize)
{
	if (Current && Current->SlotSize >= SlotSize)
	{
		return Current.Get();
	}
	const uint32 SlotCount = static_cast<uint32>(FMath::Clamp(CVarSharedMemorySlots.GetValueOnAnyThread(), 1, 64));
	TUniquePtr<FSegment> Segment = FSegment::Make(SlotCount, AlignUp(SlotSize, SlotSizeGranularity));
	if (!Segment)
	{
		return nullptr;
	}
	if (Current)
	{
		if (!Current->IsAttached())
		{
			Retired.Add(MoveTemp(Current));
		}
	}
	Current = MoveTemp(Segment);
	NextSlot = 0;
	return Current.Get();
}

bool FTempoSharedMemoryStream::Encode(const google::protobuf_tempo::Message& Response, google::protobuf_tempo::Message& Envelope)
{
#if TEMPO_HAS_SHARED_MEMORY
	if (bClosed)
	{
		return false;
	}
	ReleaseAttachedSegments();

	const FReflection* Reflection = Response.GetReflection();
	const uint64 MinBytes = static_cast<uint64>(FMath::Max(CVarSharedMemoryMinBytes.GetValueOnAnyThread(), RefSize + 1));
	TArray<TPair<const FFieldDescriptor*, const std::string*>, TInlineAllocator<16>> Payloads;
	std::string Scratch;
	uint64 SlotBytes = 0;
	for (const FFieldDescriptor* Field : BytesFields)
	{
		// The field's own string, not a copy: Scratch is only used for a Cord-backed field.
		const std::string& Value = Reflection->GetStringReference(Response, Field, &Scratch);
		if (Value.size() >= MinBytes && &Value != &Scratch)
		{
			Payloads.Emplace(Field, &Value);
			SlotBytes += AlignUp(Value.size(), PayloadAlignment);
		}
	}

	// The first message makes the segment Announce names, and it and every message after it go inline until
	// the client has mapped that segment. A client that cannot map it never does, and so gets inline
	// payloads throughout, with nothing lost.
	if (!bAnnounced && !Current)
	{
		EnsureSegment(FMath::Max<uint64>(SlotBytes, 1));
	}
	if (!bClientAttached || Payloads.IsEmpty())
	{
		return false;
	}

	FSegment* Segment = EnsureSegment(SlotBytes);
	if (!Segment)
	{
		return false;
	}
	int64 Slot = INDEX_NONE;
	for (uint32 Probe = 0; Probe < Segment->SlotCount; ++Probe)
	{
		const uint32 Candidate = (NextSlot + Probe) % Segment->SlotCount;
		if (FPlatformAtomics::AtomicRead(Segment->SlotState(Candidate)) == 0)
		{
			Slot = Candidate;
			break;
		}
	}
	if (Slot == INDEX_NONE)
	{
		if (FTempoWriteStats::IsEnabled())
		{
			FTempoWriteStats::RecordSharedMemoryFallback();
		}
		return false;
	}
	NextSlot = (Slot + 1) % Segment->SlotCount;
	const uint64 Sequence = NextSequence++;

	Envelope.Clear();
	std::vector<const FFieldDescriptor*> SetFields;
	Reflection->ListFields(Response, &SetFields);
	for (const FFieldDescriptor* Field : SetFields)
	{
		if (!Payloads.ContainsByPredicate([Field](const auto& Payload) { return Payload.Key == Field; }))
		{
			CopyField(Reflection, Response, &Envelope, Field, CppTypes.FindRef(Field));
		}
	}

	uint8* Data = Segment->SlotData(Slot);
	uint64 Offset = 0;
	int64 MovedBytes = 0;
	for (const auto& [Field, Value] : Payloads)
	{
		FMemory::Memcpy(Data + Offset, Value->data(), Value->size());

		uint8 Ref[RefSize] = {};
		Put<uint64>(Ref, 0, RefMagic);
		Put<uint32>(Ref, 8, LayoutVersion);
		Put<uint32>(Ref, 12, static_cast<uint32>(Slot));
		Put<uint64>(Ref, 16, Sequence);
		Put<uint64>(Ref, 24, (Data - Segment->Base) + Offset);
		Put<uint64>(Ref, 32, Value->size());
		Put<uint64>(Ref, 40, Segment->Size);
		FMemory::Memcpy(Ref + 48, Segment->Name, MaxNameLength);
		Reflection->SetString(&Envelope, Field, std::string(reinterpret_cast<const char*>(Ref), RefSize));

		Offset += AlignUp(Value->size(), PayloadAlignment);
		MovedBytes += static_cast<int64>(Value->size());
	}

	// The client reads the slot only after the message that names it arrives, which is sent after this.
	Put<uint64>(Segment->Base, HeaderSize + Slot * SlotEntrySize + 8, Sequence);
	FPlatformAtomics::AtomicStore(Segment->SlotState(Slot), 1);

	if (FTempoWriteStats::IsEnabled())
	{
		FTempoWriteStats::RecordSharedMemory(MovedBytes);
	}
	return true;
#else
	return false;
#endif
}

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif
