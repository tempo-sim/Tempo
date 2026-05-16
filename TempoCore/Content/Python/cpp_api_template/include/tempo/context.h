// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Connection context — global singleton that owns the gRPC channel to the
// Tempo server. Mirrors src/context.rs in the Rust API.

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <grpcpp/channel.h>

#include "tempo/result.h"

namespace tempo {

constexpr const char* kDefaultAddress = "localhost";
constexpr uint16_t kDefaultPort = 10001;

/// Maximum gRPC message size (1 GB), matching the Rust and Python clients.
constexpr int kMaxMessageSize = 1'000'000'000;

class TempoContext {
public:
	static TempoContext& instance();

	void set_server(const std::string& address, uint16_t port);

	std::string address() const;
	uint16_t port() const;

	/// Lazily-created shared gRPC channel. Returns the same channel until the
	/// server endpoint is changed.
	Result<std::shared_ptr<grpc::Channel>> channel();

private:
	TempoContext();
	TempoContext(const TempoContext&) = delete;
	TempoContext& operator=(const TempoContext&) = delete;

	mutable std::mutex mutex_;
	std::string address_;
	uint16_t port_;
	std::shared_ptr<grpc::Channel> channel_;
};

/// Set the server address and port. Resets any cached channel.
void set_server(const std::string& address, uint16_t port);

}  // namespace tempo
