// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Error type for the Tempo client API.

#pragma once

#include <string>

#include <grpcpp/support/status.h>

namespace tempo {

enum class ErrorKind {
	Connection,
	Rpc,
	InvalidUri,
	Unknown,
};

class TempoError {
public:
	static TempoError from_status(const grpc::Status& status);
	static TempoError connection(std::string message);
	static TempoError invalid_uri(std::string message);
	static TempoError unknown(std::string message);

	ErrorKind kind() const { return kind_; }
	const std::string& message() const { return message_; }

	/// gRPC status code for Rpc errors; 0 otherwise.
	int code() const { return code_; }

	/// Human-readable, single-line description.
	std::string what() const;

private:
	TempoError(ErrorKind kind, std::string message, int code);

	ErrorKind kind_;
	std::string message_;
	int code_;
};

}  // namespace tempo
