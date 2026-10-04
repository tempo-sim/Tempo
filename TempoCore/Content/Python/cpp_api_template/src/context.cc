// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/context.h"

#include <cstdlib>
#include <filesystem>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace tempo {

namespace {

#ifndef _WIN32
// The directory a bare socket name lands in. Mirrors the server's rule (see
// TempoServerEndpoint::GetDefaultSocketDirectory) so that naming a socket "sim-a.sock" means the
// same file on both sides.
std::string default_socket_directory() {
#ifdef __linux__
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != nullptr && *runtime_dir != '\0') {
        return std::string(runtime_dir) + "/tempo";
    }
#endif
    return "/tmp/tempo-" + std::to_string(static_cast<int>(getuid()));
}
#endif  // !_WIN32

// A bare name goes in the default directory; anything else is a path. gRPC resolves a relative
// "unix:" path against the client's working directory, which is rarely the server's, so the
// result is always absolute.
std::string resolve_socket_path(const std::string& path) {
    std::string resolved = path.empty() ? std::string(kDefaultSocketName) : path;
#ifndef _WIN32
    if (resolved.find('/') == std::string::npos) {
        resolved = default_socket_directory() + "/" + resolved;
    }
#endif
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(resolved, error);
    if (error) {
        return resolved;
    }
    return absolute.lexically_normal().string();
}

}  // namespace

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
    socket_path_.clear();
    channel_.reset();
}

void TempoContext::set_socket(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_path_ = resolve_socket_path(path);
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

std::string TempoContext::socket_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return socket_path_;
}

std::string TempoContext::target() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return target_locked();
}

std::string TempoContext::target_locked() const {
    if (!socket_path_.empty()) {
        // gRPC's naming scheme for a filesystem socket. The path is absolute, so the single
        // slash after the scheme is the leading slash of the path itself.
        return "unix:" + socket_path_;
    }
    return address_ + ":" + std::to_string(port_);
}

Result<std::shared_ptr<grpc::Channel>> TempoContext::channel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel_) {
        return channel_;
    }

#ifdef _WIN32
    if (!socket_path_.empty()) {
        return TempoError::connection(
            "Unix domain sockets are not supported on Windows. Use set_server(address, port).");
    }
#endif

    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(kMaxMessageSize);
    args.SetMaxSendMessageSize(kMaxMessageSize);

    const std::string endpoint = target_locked();
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

void set_socket(const std::string& path) {
    TempoContext::instance().set_socket(path);
}

}  // namespace tempo
