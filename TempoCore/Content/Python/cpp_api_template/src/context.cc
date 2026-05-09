// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/context.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

namespace tempo {

TempoContext& TempoContext::instance() {
    static TempoContext ctx;
    return ctx;
}

TempoContext::TempoContext()
    : address_(kDefaultAddress), port_(kDefaultPort) {}

void TempoContext::set_server(const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    address_ = address;
    port_ = port;
    channel_.reset();
}

std::string TempoContext::address() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return address_;
}

uint16_t TempoContext::port() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return port_;
}

Result<std::shared_ptr<grpc::Channel>> TempoContext::channel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel_) {
        return channel_;
    }

    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(kMaxMessageSize);
    args.SetMaxSendMessageSize(kMaxMessageSize);

    const std::string endpoint = address_ + ":" + std::to_string(port_);
    auto channel = grpc::CreateCustomChannel(
        endpoint, grpc::InsecureChannelCredentials(), args);
    if (!channel) {
        return TempoError::connection("Failed to create channel to " + endpoint);
    }
    channel_ = std::move(channel);
    return channel_;
}

void set_server(const std::string& address, uint16_t port) {
    TempoContext::instance().set_server(address, port);
}

}  // namespace tempo
