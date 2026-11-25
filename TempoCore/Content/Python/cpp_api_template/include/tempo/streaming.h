// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Synchronous server-streaming wrapper. Mirrors src/streaming.rs in the Rust
// API: the consumer drives the stream by calling read() in a loop until it
// returns false.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/impl/proto_utils.h>
#include <grpcpp/support/sync_stream.h>

#include "tempo/shared_memory.h"

namespace tempo {

template <typename Response>
class ServerStream {
public:
	ServerStream(std::shared_ptr<grpc::Channel> channel,
				 std::unique_ptr<grpc::ClientContext> context,
				 std::unique_ptr<grpc::ClientReader<Response>> reader)
		: channel_(std::move(channel)),
		  context_(std::move(context)),
		  reader_(std::move(reader)) {}

	/// A stream that asked for shared memory: fields lists a message's top-level bytes fields, which
	/// read() restores.
	ServerStream(std::shared_ptr<grpc::Channel> channel,
				 std::unique_ptr<grpc::ClientContext> context,
				 std::unique_ptr<grpc::ClientReader<Response>> reader,
				 std::function<std::vector<std::string*>(Response&)> fields)
		: channel_(std::move(channel)),
		  context_(std::move(context)),
		  reader_(std::move(reader)),
		  shared_memory_(std::make_unique<SharedMemoryReader>()),
		  fields_(std::move(fields)) {}

	ServerStream(ServerStream&&) noexcept = default;
	ServerStream& operator=(ServerStream&&) noexcept = default;
	ServerStream(const ServerStream&) = delete;
	ServerStream& operator=(const ServerStream&) = delete;

	/// Block until the next message arrives. Writes the result into *out and
	/// returns true; returns false when the stream ends or fails (call finish()
	/// to retrieve the terminal status).
	bool read(Response* out) {
		if (!reader_->Read(out)) {
			return false;
		}
		if (shared_memory_) {
			if (!attached_) {
				// The initial metadata arrived with the first message.
				attached_ = true;
				shared_memory_->attach(context_->GetServerInitialMetadata());
			}
			std::string error;
			if (!shared_memory_->restore(fields_(*out), &error)) {
				failure_ = grpc::Status(grpc::StatusCode::INTERNAL, error);
				cancel();
				return false;
			}
		}
		return true;
	}

	/// Cancel the stream. Safe to call from any thread.
	void cancel() {
		if (context_) {
			context_->TryCancel();
		}
	}

	/// Block until the server closes the stream. Returns the terminal status.
	grpc::Status finish() {
		grpc::Status status = reader_->Finish();
		return failure_.ok() ? status : failure_;
	}

private:
	// Declaration order is destruction-order critical: reader_ must destruct
	// before context_, and context_ before channel_.
	std::shared_ptr<grpc::Channel> channel_;
	std::unique_ptr<grpc::ClientContext> context_;
	std::unique_ptr<grpc::ClientReader<Response>> reader_;
	std::unique_ptr<SharedMemoryReader> shared_memory_;
	std::function<std::vector<std::string*>(Response&)> fields_;
	bool attached_ = false;
	grpc::Status failure_;
};

}  // namespace tempo
