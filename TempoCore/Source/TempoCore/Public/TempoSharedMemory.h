// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include <string>

namespace google::protobuf_tempo
{
	class Descriptor;
	class FieldDescriptor;
	class Message;
}

namespace grpc
{
	class ServerContext;
}

// The shared-memory side channel of one server stream, for a client on the same machine.
//
// A client asks for it with the request metadata `tempo-shm: 1`. The server answers in the initial metadata
// with the stream's first segment (see Announce), and sends every message inline until the client has mapped
// that segment and marked it attached; a client that cannot map it keeps receiving inline payloads, so
// nothing is lost. From then on, each top-level
// singular `bytes` field of at least Tempo.SharedMemory.MinBytes is copied into a slot of a ring in a
// shared-memory segment, and the message on the wire carries a fixed-size reference in that field
// instead. Every message still travels on the gRPC stream, in order and under gRPC's flow control; only
// the large payloads move. When no slot is free the message goes inline, as it would without shared
// memory, so a slow client never blocks the server.
//
// Layout, all little-endian. A segment:
//
//   header (64 bytes)
//     0  magic          u64  "TEMPOSHM"
//     8  version        u32  1
//     12 slot count     u32
//     16 slot size      u64
//     24 attached       u32  the client sets it to 1 once it has mapped the segment
//     28 (reserved)     u32
//     32 data offset    u64  where slot 0 starts
//   slot table, from 64: per slot, 16 bytes
//     0  state          u32  0 free, 1 written (the server sets 1, the client sets 0 once it has read it)
//     4  (reserved)     u32
//     8  sequence       u64  of the message the slot holds
//   slots, from the data offset: slot count x slot size bytes
//
// A reference, in place of a field's payload (RefSize bytes):
//
//     0  magic          u64  "TEMPOREF"
//     8  version        u32  1
//     12 slot           u32
//     16 sequence       u64  must match the slot's
//     24 offset         u64  of the payload, from the start of the segment
//     32 length         u64  of the payload
//     40 segment size   u64  the whole segment's, to map it
//     48 segment name   32 bytes, NUL-padded, for shm_open (with a leading '/')
//
// All the references in one message point into the same slot, which the client frees once it has
// restored every field. A message too large for the ring's slots moves the stream to a new, larger
// segment, which its references name; the client maps it and drops the one before.
//
// The server unlinks a segment's name as soon as the client has attached, so nothing is left behind if
// either process dies after that.
class TEMPOCORE_API FTempoSharedMemoryStream
{
public:
	static constexpr int32 RefSize = 80;

	// Whether the client of this call asked for shared memory and can have it: it sent `tempo-shm: 1`, is
	// on this machine (a Unix domain socket or a loopback address), and Tempo.SharedMemory is on.
	static bool IsRequested(const grpc::ServerContext& Context);

	// The channel for a stream of Descriptor's messages, or null if they have no top-level bytes field or
	// this platform has no shared memory.
	static TUniquePtr<FTempoSharedMemoryStream> Create(const google::protobuf_tempo::Descriptor* Descriptor);

	~FTempoSharedMemoryStream();

	// Sets Envelope to Response with each large bytes field moved into a free slot and replaced by its
	// reference. Returns false, leaving Envelope unspecified, if Response should go inline: it has no
	// large field, no slot is free, or a segment could not be made.
	bool Encode(const google::protobuf_tempo::Message& Response, google::protobuf_tempo::Message& Envelope);

	// Names the stream's first segment, which the first Encode made, in the call's initial metadata
	// (`tempo-shm-segment`, the name; `tempo-shm-size`, its size in bytes). Called once, after the first
	// Encode and before the first write; later calls do nothing.
	void Announce(grpc::ServerContext& Context);

	// The segment Announce names: the one the first Encode made, until the client has attached.
	bool GetFirstSegment(std::string& Name, uint64& Size) const;

	// Releases every segment. Encode returns false from here on.
	void Close();

private:
	struct FSegment;

	FTempoSharedMemoryStream(TArray<const google::protobuf_tempo::FieldDescriptor*>&& BytesFieldsIn,
		TMap<const google::protobuf_tempo::FieldDescriptor*, int32>&& CppTypesIn);

	FSegment* EnsureSegment(uint64 SlotSize);
	void ReleaseAttachedSegments();

	// The response type's top-level singular bytes fields.
	const TArray<const google::protobuf_tempo::FieldDescriptor*> BytesFields;
	// Every top-level field's FieldDescriptor::CppType.
	const TMap<const google::protobuf_tempo::FieldDescriptor*, int32> CppTypes;

	TUniquePtr<FSegment> Current;
	// Segments a client may still have to attach to, to read messages sent before a resize.
	TArray<TUniquePtr<FSegment>> Retired;
	bool bAnnounced = false;
	// Whether the client has mapped a segment of this stream. Until it has, every message goes inline.
	bool bClientAttached = false;
	uint64 NextSequence = 1;
	uint32 NextSlot = 0;
	bool bClosed = false;
};
