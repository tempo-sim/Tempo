// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// The client half of the shared-memory transport for streams with large `bytes` fields. Mirrors
// tempo_sim::shared_memory in the Rust API and tempo_sim._shared_memory in Python.
//
// A generated stream wrapper whose response type has a top-level `bytes` field asks the server for
// shared memory. A server on this machine that can offer it names the stream's first segment in the
// call's initial metadata. Once this client has mapped that segment, the server moves each large
// `bytes` field into a ring in shared memory and sends a fixed-size reference in its place.
// ServerStream::read puts the payload back, so a caller gets the same messages either way. The layout
// is documented in TempoCore's TempoSharedMemory.h.
//
// TEMPO_SHARED_MEMORY=0 turns it off. If the announced segment cannot be mapped (a client in a
// container without the sim's /dev/shm, say), the server goes on sending every payload inline, and no
// later stream in this process asks again.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <grpcpp/support/string_ref.h>

namespace tempo {

class SharedMemoryReader {
public:
	/// Whether new streams ask for shared memory.
	static bool enabled();

	SharedMemoryReader() = default;
	~SharedMemoryReader();
	SharedMemoryReader(const SharedMemoryReader&) = delete;
	SharedMemoryReader& operator=(const SharedMemoryReader&) = delete;

	/// Maps the segment the server announced in the call's initial metadata, if it announced one.
	/// Returns whether this client can now receive payloads through shared memory.
	bool attach(const std::multimap<grpc::string_ref, grpc::string_ref>& initial_metadata);

	/// Replaces each reference among fields with the payload it names, and frees its slot. Returns
	/// false, with *error set, if a reference cannot be followed.
	bool restore(const std::vector<std::string*>& fields, std::string* error);

private:
	struct Segment {
		std::string name;
		uint8_t* base = nullptr;
		size_t size = 0;
	};

	const Segment* segment(const std::string& name, size_t size, std::string* error);
	void close();

	std::vector<Segment> segments_;
};

}  // namespace tempo
