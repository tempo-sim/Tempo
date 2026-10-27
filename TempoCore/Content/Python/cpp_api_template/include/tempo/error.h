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
	/// The server answered with something the client cannot make sense of: a value of the wrong
	/// variant, a result missing from a call. A generated method and the sim disagree.
	Protocol,
	/// An op in a batch failed. The message names it.
	Batch,
	/// The generated classes do not describe the sim they are talking to (world::check_schema).
	SchemaMismatch,
	/// A read made inside world::defer, which is queued rather than performed. Only defer sees it.
	Deferred,
};

class TempoError {
public:
	static TempoError from_status(const grpc::Status& status);
	static TempoError connection(std::string message);
	static TempoError invalid_uri(std::string message);
	static TempoError unknown(std::string message);
	static TempoError protocol(std::string message);
	static TempoError batch(std::string message);
	static TempoError schema_mismatch(std::string message);
	static TempoError deferred();

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
