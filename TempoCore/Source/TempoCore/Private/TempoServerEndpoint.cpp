// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoServerEndpoint.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

#define TEMPO_UNIX_SOCKETS_SUPPORTED (PLATFORM_MAC || PLATFORM_LINUX)

#if TEMPO_UNIX_SOCKETS_SUPPORTED
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace TempoServerEndpoint
{
namespace
{
	// Used when the UnixSocket transport is selected without a path, the counterpart of the
	// default TCP port. Like the default port, two servers left on the default collide - and the
	// liveness probe reports it.
	const TCHAR* DefaultSocketName = TEXT("tempo.sock");

	bool IsBareName(const FString& Path)
	{
		return !Path.IsEmpty() &&
			!Path.Contains(TEXT("/")) &&
			!Path.Contains(TEXT("\\")) &&
			Path != TEXT(".") &&
			Path != TEXT("..");
	}

	// The length of Path as the bytes that actually go into sockaddr_un::sun_path.
	int32 Utf8Length(const FString& Path)
	{
		return FTCHARToUTF8(*Path).Length();
	}

#if TEMPO_UNIX_SOCKETS_SUPPORTED
	// Fills Addr with Path. False if Path does not fit, which ResolveSocketPath rejects earlier.
	bool MakeSocketAddress(const FString& Path, sockaddr_un& OutAddr)
	{
		const FTCHARToUTF8 Utf8Path(*Path);
		if (Utf8Path.Length() >= static_cast<int32>(UE_ARRAY_COUNT(OutAddr.sun_path)))
		{
			return false;
		}
		FMemory::Memzero(&OutAddr, sizeof(OutAddr));
		OutAddr.sun_family = AF_UNIX;
		FMemory::Memcpy(OutAddr.sun_path, Utf8Path.Get(), Utf8Path.Length());
		return true;
	}
#endif
}

bool IsUnixSocketTransportSupported()
{
	return TEMPO_UNIX_SOCKETS_SUPPORTED;
}

FString GetDefaultSocketDirectory()
{
#if PLATFORM_LINUX
	// Set for a logged-in user on any systemd distro. Already 0700 and user-owned, and cleared
	// when the session ends, so sockets do not outlive the login.
	const FString RuntimeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_RUNTIME_DIR"));
	if (!RuntimeDir.IsEmpty())
	{
		return FPaths::Combine(RuntimeDir, TEXT("tempo"));
	}
#endif
#if TEMPO_UNIX_SOCKETS_SUPPORTED
	// Per-user so two users on one machine cannot land on the same path. /tmp is world-writable,
	// which is why EnsureSocketDirectory makes the directory it creates 0700.
	return FString::Printf(TEXT("/tmp/tempo-%d"), static_cast<int32>(getuid()));
#else
	return FString();
#endif
}

bool ResolveSocketPath(const FString& SocketPath, FString& OutResolvedPath, FString& OutError)
{
	OutResolvedPath.Reset();

	const FString Requested = SocketPath.TrimStartAndEnd();
	FString Resolved;
	if (Requested.IsEmpty())
	{
		Resolved = FPaths::Combine(GetDefaultSocketDirectory(), DefaultSocketName);
	}
	else if (IsBareName(Requested))
	{
		Resolved = FPaths::Combine(GetDefaultSocketDirectory(), Requested);
	}
	else
	{
		Resolved = FPaths::ConvertRelativePathToFull(Requested);
	}
	FPaths::NormalizeFilename(Resolved);
	FPaths::CollapseRelativeDirectories(Resolved);

	// An AF_UNIX address cannot carry a longer path, so neither this server nor any client could
	// use it. Deep project directories reach this easily, hence the short default directory.
	const int32 Length = Utf8Length(Resolved);
	if (Length >= MaxSocketPathLength)
	{
		OutError = FString::Printf(
			TEXT("socket path is %d bytes, but this platform allows at most %d: %s. ")
			TEXT("Use a shorter path, or a bare name to put the socket in %s."),
			Length, MaxSocketPathLength - 1, *Resolved, *GetDefaultSocketDirectory());
		return false;
	}

#if TEMPO_UNIX_SOCKETS_SUPPORTED
	// gRPC removes a stale socket file before binding, but it leaves anything that is not a
	// socket in place and the bind then fails with an unhelpful error.
	struct stat PathStat;
	if (stat(TCHAR_TO_UTF8(*Resolved), &PathStat) == 0 && !S_ISSOCK(PathStat.st_mode))
	{
		OutError = FString::Printf(TEXT("%s exists and is not a socket"), *Resolved);
		return false;
	}
#endif

	OutResolvedPath = MoveTemp(Resolved);
	return true;
}

bool ResolveEndpoint(EServerTransport Transport, int32 Port, const FString& SocketPath, FEndpoint& OutEndpoint, FString& OutError)
{
	OutEndpoint = FEndpoint();

	if (Transport == EServerTransport::UnixSocket)
	{
		if (!IsUnixSocketTransportSupported())
		{
			OutError = TEXT("the UnixSocket server transport is not supported on this platform. Use Tcp.");
			return false;
		}

		FString ResolvedPath;
		if (!ResolveSocketPath(SocketPath, ResolvedPath, OutError))
		{
			return false;
		}

		// gRPC's naming scheme for a filesystem socket. The path is absolute, so the single
		// slash after the scheme is the leading slash of the path itself.
		OutEndpoint.Target = FString::Printf(TEXT("unix:%s"), *ResolvedPath);
		OutEndpoint.SocketPath = MoveTemp(ResolvedPath);
		return true;
	}

	if (Port < 1024 || Port > 65535)
	{
		OutError = FString::Printf(TEXT("server port %d is out of range (1024-65535)"), Port);
		return false;
	}

	// All interfaces, so a client on another machine can reach the server too.
	OutEndpoint.Target = FString::Printf(TEXT("0.0.0.0:%d"), Port);
	return true;
}

bool EnsureSocketDirectory(const FString& AbsoluteSocketPath, FString& OutError)
{
	const FString Directory = FPaths::GetPath(AbsoluteSocketPath);
	if (Directory.IsEmpty() || IFileManager::Get().DirectoryExists(*Directory))
	{
		return true;
	}

	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("could not create socket directory %s"), *Directory);
		return false;
	}

#if TEMPO_UNIX_SOCKETS_SUPPORTED
	// A socket's own permissions come from the process umask, which is usually too permissive to
	// rely on, so access control lives on the directory instead. Only the directory we just
	// created is narrowed - a directory the user pointed us at keeps the permissions it has.
	if (chmod(TCHAR_TO_UTF8(*Directory), S_IRWXU) != 0)
	{
		OutError = FString::Printf(TEXT("could not make socket directory %s user-private"), *Directory);
		return false;
	}
#endif

	return true;
}

bool IsSocketPathInUse(const FString& AbsoluteSocketPath)
{
#if TEMPO_UNIX_SOCKETS_SUPPORTED
	sockaddr_un Addr;
	if (!MakeSocketAddress(AbsoluteSocketPath, Addr))
	{
		return false;
	}

	const int32 Socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (Socket < 0)
	{
		return false;
	}

	// Succeeds only against a socket with a live listener. A leftover file from a crashed server
	// gives ECONNREFUSED, which is what lets gRPC go ahead and replace it.
	const bool bInUse = connect(Socket, reinterpret_cast<const sockaddr*>(&Addr), sizeof(Addr)) == 0;
	close(Socket);
	return bInUse;
#else
	return false;
#endif
}

void RemoveSocketFile(const FString& AbsoluteSocketPath)
{
#if TEMPO_UNIX_SOCKETS_SUPPORTED
	struct stat PathStat;
	if (stat(TCHAR_TO_UTF8(*AbsoluteSocketPath), &PathStat) == 0 && S_ISSOCK(PathStat.st_mode))
	{
		unlink(TCHAR_TO_UTF8(*AbsoluteSocketPath));
	}
#endif
}
}
