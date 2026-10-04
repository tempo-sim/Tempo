// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCoreTypes.h"

#include "CoreMinimal.h"

/**
 * Resolution and lifecycle of the endpoint the Tempo gRPC server listens on.
 *
 * The transport is exclusive: the server listens on a TCP port or on a Unix domain socket, never
 * both. Everything here is free-standing (no gRPC, no engine objects) so the resolution rules are
 * unit-testable without starting a server - see Tempo.Core.ServerEndpoint.
 */
namespace TempoServerEndpoint
{
	/**
	 * A resolved listening endpoint.
	 */
	struct FEndpoint
	{
		// The target URI to hand to grpc::ServerBuilder::AddListeningPort.
		FString Target;

		// For the UnixSocket transport, the absolute path Target refers to. Empty for Tcp.
		// Held by the server so it can remove the socket file on shutdown.
		FString SocketPath;
	};

	// The longest socket path an AF_UNIX address can carry, including its null terminator: the
	// size of sockaddr_un::sun_path, which differs by platform. A path at or over this length
	// cannot be bound or connected to at all, so it is rejected during resolution rather than
	// surfacing as an opaque bind failure.
	constexpr int32 MaxSocketPathLength = PLATFORM_MAC ? 104 : 108;

	// Whether this platform can bind a Unix domain socket. False on Windows: the vendored gRPC
	// (1.62) has no AF_UNIX support there, and neither do the grpcio/tonic clients.
	TEMPOCORE_API bool IsUnixSocketTransportSupported();

	// The directory a bare socket name resolves into: $XDG_RUNTIME_DIR/tempo (Linux, already
	// user-private and cleaned up at logout) or /tmp/tempo-<uid> (macOS, and Linux without
	// XDG_RUNTIME_DIR). Both are short, which matters against MaxSocketPathLength.
	TEMPOCORE_API FString GetDefaultSocketDirectory();

	// Turns a configured socket path into an absolute, bindable one.
	//
	// A bare name ("sim-a.sock") lands in GetDefaultSocketDirectory(), so instances can be named
	// rather than numbered. Anything else is treated as a path: absolute paths are used as given,
	// relative ones resolve against the working directory as usual. An empty path yields the
	// default name in the default directory, the counterpart of the default TCP port.
	//
	// Returns false and fills OutError if the result could not be used (too long for sun_path, or
	// occupied by something that is not a socket).
	TEMPOCORE_API bool ResolveSocketPath(const FString& SocketPath, FString& OutResolvedPath, FString& OutError);

	// Resolves the configured transport into the endpoint to listen on.
	// Returns false and fills OutError if the configuration cannot be used on this platform.
	TEMPOCORE_API bool ResolveEndpoint(EServerTransport Transport, int32 Port, const FString& SocketPath, FEndpoint& OutEndpoint, FString& OutError);

	// Creates the directory holding SocketPath, user-private (0700) if we create it.
	TEMPOCORE_API bool EnsureSocketDirectory(const FString& AbsoluteSocketPath, FString& OutError);

	// Whether a live server is already accepting connections at AbsoluteSocketPath.
	//
	// gRPC unlinks an existing socket file before binding it, so a bind never fails the way a
	// taken TCP port does - a second server would quietly take the name and leave the first
	// running but unreachable. Probing for a live owner first keeps the "refuse to share an
	// endpoint" behavior that GRPC_ARG_ALLOW_REUSEPORT=0 gives the TCP transport.
	//
	// This connects and immediately closes, which a listening server sees as a client that hung
	// up. That only happens while a second server is starting up, and gRPC ignores it.
	TEMPOCORE_API bool IsSocketPathInUse(const FString& AbsoluteSocketPath);

	// Removes the socket file at AbsoluteSocketPath, if one is there. gRPC leaves the file behind
	// on shutdown, so the server cleans up after itself; anything that is not a socket is left
	// alone.
	TEMPOCORE_API void RemoveSocketFile(const FString& AbsoluteSocketPath);
}
