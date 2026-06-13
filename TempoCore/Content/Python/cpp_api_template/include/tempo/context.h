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

/// Which transport the client should use. UDS is the default since the Tempo server
/// always listens on a same-host Unix-domain socket alongside TCP.
enum class Transport { kUnixSocket, kTcp };

/// Return the default Unix-domain-socket path the Tempo server listens on for the
/// given port. Mirrors UTempoCoreUtils::GetDefaultUnixSocketPath so the client picks
/// up the same socket the server creates without any explicit configuration.
std::string default_unix_socket_path(uint16_t port);

class TempoContext {
public:
	static TempoContext& instance();

	/// Switch to TCP at the given address and port. Resets any cached channel.
	void set_server(const std::string& address, uint16_t port);

	/// Switch to a Unix-domain socket at `path`. Resets any cached channel.
	void set_unix_socket(const std::string& path);

	Transport transport() const;
	std::string address() const;
	uint16_t port() const;
	std::string unix_socket_path() const;

	/// Lazily-created shared gRPC channel. Returns the same channel until the
	/// transport configuration is changed.
	Result<std::shared_ptr<grpc::Channel>> channel();

private:
	TempoContext();
	TempoContext(const TempoContext&) = delete;
	TempoContext& operator=(const TempoContext&) = delete;

	mutable std::mutex mutex_;
	Transport transport_;
	std::string address_;
	uint16_t port_;
	std::string unix_socket_path_;
	std::shared_ptr<grpc::Channel> channel_;
};

/// Switch to TCP at the given address and port. Resets any cached channel.
void set_server(const std::string& address, uint16_t port);

/// Switch to a Unix-domain socket at `path`. Resets any cached channel.
void set_unix_socket(const std::string& path);

}  // namespace tempo
