// Copyright Tempo Simulation, LLC. All Rights Reserved

// Where Tempo's gRPC is a shared library of its own, this file is compiled into that library
// (TempoCore/Scripts/LinkGrpcShared.sh) rather than into TempoCore, and TempoCore's build leaves it empty.
#include "TempoGrpcServer.h"

#if TEMPO_GRPC_SERVER_IN_SHARED_LIBRARY || !TEMPO_GRPC_IS_SHARED_LIBRARY

namespace TempoGrpc
{
	FServer BuildAndStartServer(const std::string& Address, const std::vector<grpc::Service*>& Services, grpc_compression_level CompressionLevel)
	{
		grpc::ServerBuilder Builder;
		Builder.AddListeningPort(Address, grpc::InsecureServerCredentials());
		for (grpc::Service* Service : Services)
		{
			Builder.RegisterService(Service);
		}
		Builder.SetDefaultCompressionLevel(CompressionLevel);

		FServer Result;
		Result.CompletionQueue = Builder.AddCompletionQueue();
		Result.Server = Builder.BuildAndStart();
		return Result;
	}

	void Shutdown(grpc::CompletionQueue& CompletionQueue)
	{
		CompletionQueue.Shutdown();
	}

	bool Next(grpc::CompletionQueue& CompletionQueue, void** OutTag, bool* bOutOk)
	{
		return CompletionQueue.Next(OutTag, bOutOk);
	}

	grpc::CompletionQueue::NextStatus AsyncNext(grpc::CompletionQueue& CompletionQueue, void** OutTag, bool* bOutOk, gpr_timespec Deadline)
	{
		return CompletionQueue.AsyncNext(OutTag, bOutOk, Deadline);
	}
}

#endif
