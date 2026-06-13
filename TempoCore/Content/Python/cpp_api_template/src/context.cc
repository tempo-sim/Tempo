// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/context.h"

#include <cstdlib>
#include <sstream>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

namespace tempo {

std::string default_unix_socket_path(uint16_t port) {
    std::ostringstream filename;
    filename << "tempo-" << port << ".sock";
#if defined(_WIN32)
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    std::string base = local_app_data ? std::string(local_app_data) : "C:\\Windows\\Temp";
    return base + "\\Temp\\" + filename.str();
#else
    return std::string("/tmp/") + filename.str();
#endif
}

TempoContext& TempoContext::instance() {
    static TempoContext ctx;
    return ctx;
}

TempoContext::TempoContext()
    : transport_(Transport::kUnixSocket),
      address_(kDefaultAddress),
      port_(kDefaultPort),
      unix_socket_path_(default_unix_socket_path(kDefaultPort)) {}

void TempoContext::set_server(const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    transport_ = Transport::kTcp;
    address_ = address;
    port_ = port;
    channel_.reset();
}

void TempoContext::set_unix_socket(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    transport_ = Transport::kUnixSocket;
    unix_socket_path_ = path;
    channel_.reset();
}

Transport TempoContext::transport() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return transport_;
}

std::string TempoContext::address() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return address_;
}

uint16_t TempoContext::port() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return port_;
}

std::string TempoContext::unix_socket_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return unix_socket_path_;
}

Result<std::shared_ptr<grpc::Channel>> TempoContext::channel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel_) {
        return channel_;
    }

    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(kMaxMessageSize);
    args.SetMaxSendMessageSize(kMaxMessageSize);

    std::string endpoint;
    if (transport_ == Transport::kUnixSocket) {
        endpoint = "unix:" + unix_socket_path_;
    } else {
        endpoint = address_ + ":" + std::to_string(port_);
    }

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

void set_unix_socket(const std::string& path) {
    TempoContext::instance().set_unix_socket(path);
}

}  // namespace tempo
