// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/error.h"

#include <utility>

namespace tempo {

TempoError::TempoError(ErrorKind kind, std::string message, int code)
    : kind_(kind), message_(std::move(message)), code_(code) {}

TempoError TempoError::from_status(const grpc::Status& status) {
    return TempoError(ErrorKind::Rpc, status.error_message(),
                      static_cast<int>(status.error_code()));
}

TempoError TempoError::connection(std::string message) {
    return TempoError(ErrorKind::Connection, std::move(message), 0);
}

TempoError TempoError::invalid_uri(std::string message) {
    return TempoError(ErrorKind::InvalidUri, std::move(message), 0);
}

TempoError TempoError::unknown(std::string message) {
    return TempoError(ErrorKind::Unknown, std::move(message), 0);
}

std::string TempoError::what() const {
    switch (kind_) {
        case ErrorKind::Connection: return "Connection error: " + message_;
        case ErrorKind::Rpc:        return "RPC error (code " + std::to_string(code_) + "): " + message_;
        case ErrorKind::InvalidUri: return "Invalid URI: " + message_;
        case ErrorKind::Unknown:    return "Error: " + message_;
    }
    return message_;
}

}  // namespace tempo
