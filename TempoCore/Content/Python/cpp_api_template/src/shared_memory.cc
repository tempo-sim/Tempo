// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/shared_memory.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#define TEMPO_HAS_SHARED_MEMORY 1
#else
#define TEMPO_HAS_SHARED_MEMORY 0
#endif

namespace tempo {

namespace {

constexpr size_t kRefSize = 80;
constexpr char kRefMagic[] = "TEMPOREF";
constexpr char kSegmentMagic[] = "TEMPOSHM";
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 64;
constexpr size_t kSlotEntrySize = 16;
constexpr size_t kAttachedOffset = 24;

std::atomic<bool> g_unavailable{false};

template <typename T>
T read_le(const uint8_t* at) {
	T value;
	std::memcpy(&value, at, sizeof(T));
	return value;
}

// The slot and attachment words are shared with the server, which uses atomics on them too.
#if TEMPO_HAS_SHARED_MEMORY
uint32_t load_acquire(uint32_t* word) { return __atomic_load_n(word, __ATOMIC_ACQUIRE); }
void store_release(uint32_t* word, uint32_t value) { __atomic_store_n(word, value, __ATOMIC_RELEASE); }
#else
uint32_t load_acquire(uint32_t* word) { return std::atomic_ref<uint32_t>(*word).load(std::memory_order_acquire); }
void store_release(uint32_t* word, uint32_t value) { std::atomic_ref<uint32_t>(*word).store(value, std::memory_order_release); }
#endif

std::string metadata_value(const std::multimap<grpc::string_ref, grpc::string_ref>& metadata, const char* key) {
	const auto found = metadata.find(key);
	return found == metadata.end() ? std::string() : std::string(found->second.data(), found->second.size());
}

}  // namespace

bool SharedMemoryReader::enabled() {
	if (!TEMPO_HAS_SHARED_MEMORY || g_unavailable.load(std::memory_order_relaxed)) {
		return false;
	}
	const char* setting = std::getenv("TEMPO_SHARED_MEMORY");
	return setting == nullptr || std::strcmp(setting, "0") != 0;
}

SharedMemoryReader::~SharedMemoryReader() {
	close();
}

void SharedMemoryReader::close() {
#if TEMPO_HAS_SHARED_MEMORY
	for (const Segment& segment : segments_) {
		munmap(segment.base, segment.size);
	}
#endif
	segments_.clear();
}

const SharedMemoryReader::Segment* SharedMemoryReader::segment(const std::string& name, size_t size, std::string* error) {
	for (const Segment& segment : segments_) {
		if (segment.name == name) {
			return &segment;
		}
	}
#if TEMPO_HAS_SHARED_MEMORY
	const int fd = shm_open(name.c_str(), O_RDWR, 0);
	if (fd < 0) {
		*error = "could not open " + name + ": " + std::strerror(errno);
		return nullptr;
	}
	void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	::close(fd);
	if (mapped == MAP_FAILED) {
		*error = "could not map " + name + ": " + std::strerror(errno);
		return nullptr;
	}
	uint8_t* base = static_cast<uint8_t*>(mapped);
	if (size < kHeaderSize || std::memcmp(base, kSegmentMagic, 8) != 0 || read_le<uint32_t>(base + 8) != kVersion) {
		munmap(base, size);
		*error = name + " has a layout this client does not know";
		return nullptr;
	}
	store_release(reinterpret_cast<uint32_t*>(base + kAttachedOffset), 1u);
	// A stream moves to a new segment only after its last message in the old one.
	close();
	segments_.push_back(Segment{name, base, size});
	return &segments_.back();
#else
	(void)size;
	*error = "no shared memory on this platform";
	return nullptr;
#endif
}

bool SharedMemoryReader::attach(const std::multimap<grpc::string_ref, grpc::string_ref>& initial_metadata) {
	const std::string name = metadata_value(initial_metadata, "tempo-shm-segment");
	if (name.empty()) {
		return false;
	}
	const size_t size = static_cast<size_t>(std::strtoull(metadata_value(initial_metadata, "tempo-shm-size").c_str(), nullptr, 10));
	std::string error;
	if (segment(name, size, &error) == nullptr) {
		g_unavailable.store(true, std::memory_order_relaxed);
		std::cerr << "Tempo shared memory is unavailable (" << error << "); payloads arrive inline." << std::endl;
		return false;
	}
	return true;
}

bool SharedMemoryReader::restore(const std::vector<std::string*>& fields, std::string* error) {
	uint32_t* released = nullptr;
	for (std::string* field : fields) {
		if (field->size() != kRefSize || std::memcmp(field->data(), kRefMagic, 8) != 0) {
			continue;
		}
		const uint8_t* ref = reinterpret_cast<const uint8_t*>(field->data());
		const uint32_t version = read_le<uint32_t>(ref + 8);
		if (version != kVersion) {
			*error = "a shared-memory reference has version " + std::to_string(version);
			return false;
		}
		const uint32_t slot = read_le<uint32_t>(ref + 12);
		const uint64_t sequence = read_le<uint64_t>(ref + 16);
		const uint64_t offset = read_le<uint64_t>(ref + 24);
		const uint64_t length = read_le<uint64_t>(ref + 32);
		const uint64_t size = read_le<uint64_t>(ref + 40);
		const char* name_begin = reinterpret_cast<const char*>(ref + 48);
		const std::string name(name_begin, strnlen(name_begin, 32));

		const Segment* mapped = segment(name, static_cast<size_t>(size), error);
		if (mapped == nullptr) {
			return false;
		}
		const size_t entry = kHeaderSize + slot * kSlotEntrySize;
		if (entry + kSlotEntrySize > mapped->size || offset + length > mapped->size) {
			*error = "a shared-memory reference is out of bounds";
			return false;
		}
		uint32_t* state = reinterpret_cast<uint32_t*>(mapped->base + entry);
		const uint32_t current = load_acquire(state);
		const uint64_t slot_sequence = read_le<uint64_t>(mapped->base + entry + 8);
		if (current != 1 || slot_sequence != sequence) {
			*error = "shared-memory slot " + std::to_string(slot) + " holds message " + std::to_string(slot_sequence)
				+ ", not " + std::to_string(sequence);
			return false;
		}
		field->assign(reinterpret_cast<const char*>(mapped->base + offset), static_cast<size_t>(length));
		released = state;
	}
	if (released != nullptr) {
		store_release(released, 0u);
	}
	return true;
}

}  // namespace tempo
