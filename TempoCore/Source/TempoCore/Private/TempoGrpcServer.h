// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

// The parts of gRPC's C++ server API that are compiled, as opposed to inline in its headers.
//
// gRPC's C++ library is built with hidden visibility, so a shared library of gRPC does not export
// them, and code outside of it cannot call them. These functions are compiled into whatever holds
// Tempo's one copy of gRPC (see TempoGrpcServer.cpp) and exported from it.
//
// This header and its .cpp use only gRPC and the standard library, never Unreal, because they are
// also compiled outside of Unreal's build.

#include <memory>
#include <string>
#include <vector>

#include "grpcpp/grpcpp.h"

// Defined to 1 only when this is compiled into the shared library.
#ifndef TEMPO_GRPC_SERVER_IN_SHARED_LIBRARY
#define TEMPO_GRPC_SERVER_IN_SHARED_LIBRARY 0
#endif

#if TEMPO_GRPC_SERVER_IN_SHARED_LIBRARY
#define TEMPO_GRPC_SERVER_API __attribute__((visibility("default")))
#else
#define TEMPO_GRPC_SERVER_API
#endif

namespace TempoGrpc
{
	struct FServer
	{
		std::unique_ptr<grpc::Server> Server;
		std::unique_ptr<grpc::ServerCompletionQueue> CompletionQueue;
	};

	// Starts a server without transport security listening on Address for Services. The server is
	// null if it could not start, which usually means the port is taken. bDisableReusePort turns off
	// SO_REUSEPORT, which gRPC enables by default where supported; it is meaningless for a Unix
	// domain socket.
	TEMPO_GRPC_SERVER_API FServer BuildAndStartServer(const std::string& Address, const std::vector<grpc::Service*>& Services, grpc_compression_level CompressionLevel, bool bDisableReusePort);

	TEMPO_GRPC_SERVER_API void Shutdown(grpc::CompletionQueue& CompletionQueue);

	// grpc::CompletionQueue::Next
	TEMPO_GRPC_SERVER_API bool Next(grpc::CompletionQueue& CompletionQueue, void** OutTag, bool* bOutOk);

	// grpc::CompletionQueue::AsyncNext
	TEMPO_GRPC_SERVER_API grpc::CompletionQueue::NextStatus AsyncNext(grpc::CompletionQueue& CompletionQueue, void** OutTag, bool* bOutOk, gpr_timespec Deadline);
}
