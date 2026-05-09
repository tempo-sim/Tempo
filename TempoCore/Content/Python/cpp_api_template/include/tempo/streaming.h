// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Synchronous server-streaming wrapper. Mirrors src/streaming.rs in the Rust
// API: the consumer drives the stream by calling read() in a loop until it
// returns false.

#pragma once

#include <memory>
#include <utility>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/impl/proto_utils.h>
#include <grpcpp/support/sync_stream.h>

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

    ServerStream(ServerStream&&) noexcept = default;
    ServerStream& operator=(ServerStream&&) noexcept = default;
    ServerStream(const ServerStream&) = delete;
    ServerStream& operator=(const ServerStream&) = delete;

    /// Block until the next message arrives. Writes the result into *out and
    /// returns true; returns false when the stream ends or fails (call finish()
    /// to retrieve the terminal status).
    bool read(Response* out) {
        return reader_->Read(out);
    }

    /// Cancel the stream. Safe to call from any thread.
    void cancel() {
        if (context_) {
            context_->TryCancel();
        }
    }

    /// Block until the server closes the stream. Returns the terminal status.
    grpc::Status finish() {
        return reader_->Finish();
    }

private:
    // Declaration order is destruction-order critical: reader_ must destruct
    // before context_, and context_ before channel_.
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<grpc::ClientContext> context_;
    std::unique_ptr<grpc::ClientReader<Response>> reader_;
};

}  // namespace tempo
