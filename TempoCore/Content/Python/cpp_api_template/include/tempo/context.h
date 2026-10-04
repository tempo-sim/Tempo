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

/// The socket name a server uses when the UnixSocket transport is selected without a path.
constexpr const char* kDefaultSocketName = "tempo.sock";

/// Maximum gRPC message size (1 GB), matching the Rust and Python clients.
constexpr int kMaxMessageSize = 1'000'000'000;

class TempoContext {
public:
	static TempoContext& instance();

	void set_server(const std::string& address, uint16_t port);

	/// Connect over a Unix domain socket instead of TCP, for a server on this machine using the
	/// UnixSocket transport. A bare name ("sim-a.sock") resolves into the same per-user directory
	/// the server uses ($XDG_RUNTIME_DIR/tempo on Linux, /tmp/tempo-<uid> on macOS); anything else
	/// is a path. An empty path means the default name in that directory. Not available on
	/// Windows, where channel() reports the error.
	void set_socket(const std::string& path = "");

	std::string address() const;
	uint16_t port() const;

	/// The resolved socket path, or empty when connecting over TCP.
	std::string socket_path() const;

	/// The gRPC target this context connects to, for logs and error messages.
	std::string target() const;

	/// Lazily-created shared gRPC channel. Returns the same channel until the
	/// server endpoint is changed.
	Result<std::shared_ptr<grpc::Channel>> channel();

private:
	TempoContext();
	TempoContext(const TempoContext&) = delete;
	TempoContext& operator=(const TempoContext&) = delete;

	// Called with mutex_ held.
	std::string target_locked() const;

	mutable std::mutex mutex_;
	std::string address_;
	uint16_t port_;
	// Non-empty when connecting over a Unix domain socket, which takes precedence over
	// address_/port_. Only one transport is used at a time, mirroring the server.
	std::string socket_path_;
	std::shared_ptr<grpc::Channel> channel_;
};

/// Set the server address and port. Resets any cached channel.
void set_server(const std::string& address, uint16_t port);

/// Connect over a Unix domain socket. Resets any cached channel.
/// See TempoContext::set_socket for how the path is resolved.
void set_socket(const std::string& path = "");

}  // namespace tempo
