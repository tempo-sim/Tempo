// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoServerEndpoint.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// Unit tests for how the Tempo gRPC server's listening endpoint is resolved and guarded: the
// target URI per transport, the socket path rules (bare name vs path, the sun_path length limit),
// and the liveness probe that keeps a second server from stealing a socket in use. No world and no
// RHI, so these run headlessly via:
//   Scripts/Test.sh            (runs all "Tempo." automation tests)
//   Automation RunTests Tempo.Core.ServerEndpoint   (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{
	constexpr EAutomationTestFlags TempoServerEndpointTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

#if PLATFORM_MAC || PLATFORM_LINUX
	// IFileManager::FileExists reports regular files only, so it never sees a socket. These tests
	// are about sockets specifically, hence the direct stat.
	bool SocketFileExists(const FString& Path)
	{
		struct stat PathStat;
		return stat(TCHAR_TO_UTF8(*Path), &PathStat) == 0 && S_ISSOCK(PathStat.st_mode);
	}
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoServerEndpointTcpTargetTest,
	"Tempo.Core.ServerEndpoint.TcpTarget", TempoServerEndpointTestFlags)
bool FTempoServerEndpointTcpTargetTest::RunTest(const FString& Parameters)
{
	TempoServerEndpoint::FEndpoint Endpoint;
	FString Error;

	if (!TempoServerEndpoint::ResolveEndpoint(EServerTransport::Tcp, 10001, FString(), Endpoint, Error))
	{
		AddError(FString::Printf(TEXT("Tcp/10001 should resolve, but failed: %s"), *Error));
		return true;
	}
	TestEqual(TEXT("Tcp target listens on all interfaces"), Endpoint.Target, FString(TEXT("0.0.0.0:10001")));
	TestTrue(TEXT("Tcp endpoint has no socket path"), Endpoint.SocketPath.IsEmpty());

	// A socket path left over in the settings is inert while the transport is Tcp.
	if (TempoServerEndpoint::ResolveEndpoint(EServerTransport::Tcp, 10002, TEXT("ignored.sock"), Endpoint, Error))
	{
		TestEqual(TEXT("Socket path is ignored for Tcp"), Endpoint.Target, FString(TEXT("0.0.0.0:10002")));
		TestTrue(TEXT("Tcp endpoint still has no socket path"), Endpoint.SocketPath.IsEmpty());
	}
	else
	{
		AddError(FString::Printf(TEXT("Tcp/10002 should resolve, but failed: %s"), *Error));
	}

	// -ServerPort= bypasses the settings' clamp, so the range is enforced here instead.
	for (const int32 OutOfRange : { 0, 80, 1023, 65536, 70000, -1 })
	{
		if (TempoServerEndpoint::ResolveEndpoint(EServerTransport::Tcp, OutOfRange, FString(), Endpoint, Error))
		{
			AddError(FString::Printf(TEXT("Port %d should have been rejected"), OutOfRange));
		}
		else
		{
			TestFalse(TEXT("A rejected port reports why"), Error.IsEmpty());
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoServerEndpointSocketPathTest,
	"Tempo.Core.ServerEndpoint.SocketPath", TempoServerEndpointTestFlags)
bool FTempoServerEndpointSocketPathTest::RunTest(const FString& Parameters)
{
	TempoServerEndpoint::FEndpoint Endpoint;
	FString Error;

	if (!TempoServerEndpoint::IsUnixSocketTransportSupported())
	{
		// Windows: selecting the transport must fail with an explanation rather than fall back to
		// TCP, which would leave the server listening somewhere the user did not ask for.
		if (TempoServerEndpoint::ResolveEndpoint(EServerTransport::UnixSocket, 10001, TEXT("sim.sock"), Endpoint, Error))
		{
			AddError(TEXT("UnixSocket transport should be rejected on this platform"));
		}
		else
		{
			TestFalse(TEXT("The rejection explains itself"), Error.IsEmpty());
		}
		return true;
	}

	const FString DefaultDirectory = TempoServerEndpoint::GetDefaultSocketDirectory();
	TestFalse(TEXT("There is a default socket directory"), DefaultDirectory.IsEmpty());

	auto Resolve = [this, &Endpoint, &Error](const FString& SocketPath) -> bool
	{
		if (!TempoServerEndpoint::ResolveEndpoint(EServerTransport::UnixSocket, 10001, SocketPath, Endpoint, Error))
		{
			AddError(FString::Printf(TEXT("'%s' should resolve, but failed: %s"), *SocketPath, *Error));
			return false;
		}
		// The path is absolute by here, so gRPC's "unix:" scheme is followed directly by it.
		TestEqual(FString::Printf(TEXT("'%s' resolves to a unix: target"), *SocketPath),
			Endpoint.Target, FString::Printf(TEXT("unix:%s"), *Endpoint.SocketPath));
		TestFalse(TEXT("The resolved path is absolute"), FPaths::IsRelative(Endpoint.SocketPath));
		return true;
	};

	// A bare name is the ergonomic case: name an instance instead of numbering a port.
	if (Resolve(TEXT("sim-a.sock")))
	{
		TestEqual(TEXT("A bare name lands in the default directory"),
			Endpoint.SocketPath, FPaths::Combine(DefaultDirectory, TEXT("sim-a.sock")));
	}

	// No path at all is the counterpart of the default port.
	if (Resolve(FString()))
	{
		TestEqual(TEXT("An empty path uses the default name in the default directory"),
			Endpoint.SocketPath, FPaths::Combine(DefaultDirectory, TEXT("tempo.sock")));
	}
	if (Resolve(TEXT("   ")))
	{
		TestEqual(TEXT("A whitespace-only path is treated as empty"),
			Endpoint.SocketPath, FPaths::Combine(DefaultDirectory, TEXT("tempo.sock")));
	}

	// Anything with a separator is a path, used as given.
	if (Resolve(TEXT("/tmp/tempo-explicit.sock")))
	{
		TestEqual(TEXT("An absolute path is used as given"),
			Endpoint.SocketPath, FString(TEXT("/tmp/tempo-explicit.sock")));
	}
	if (Resolve(TEXT("/tmp/./sub/../tempo-collapse.sock")))
	{
		TestEqual(TEXT("An absolute path is collapsed"),
			Endpoint.SocketPath, FString(TEXT("/tmp/tempo-collapse.sock")));
	}

	// Longer than sockaddr_un::sun_path can carry: unusable by any client, so it is refused here
	// rather than surfacing as an opaque bind failure.
	const FString TooLong = FString::Printf(TEXT("/tmp/%s.sock"),
		*FString::ChrN(TempoServerEndpoint::MaxSocketPathLength, TEXT('x')));
	if (TempoServerEndpoint::ResolveEndpoint(EServerTransport::UnixSocket, 10001, TooLong, Endpoint, Error))
	{
		AddError(TEXT("An over-long socket path should have been rejected"));
	}
	else
	{
		TestTrue(TEXT("The length rejection names the limit"),
			Error.Contains(FString::FromInt(TempoServerEndpoint::MaxSocketPathLength - 1)));
	}

	// A regular file in the way would not be removed by gRPC, so it is refused too.
	const FString RegularFile = FString::Printf(TEXT("/tmp/tempo-not-a-socket-%u"), FPlatformProcess::GetCurrentProcessId());
	if (TUniquePtr<FArchive> Writer{ IFileManager::Get().CreateFileWriter(*RegularFile) })
	{
		Writer->Close();
		Writer.Reset();
		FString ResolvedPath;
		if (TempoServerEndpoint::ResolveSocketPath(RegularFile, ResolvedPath, Error))
		{
			AddError(TEXT("A path occupied by a regular file should have been rejected"));
		}
		else
		{
			TestFalse(TEXT("The occupancy rejection explains itself"), Error.IsEmpty());
		}
		IFileManager::Get().Delete(*RegularFile);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoServerEndpointInUseProbeTest,
	"Tempo.Core.ServerEndpoint.InUseProbe", TempoServerEndpointTestFlags)
bool FTempoServerEndpointInUseProbeTest::RunTest(const FString& Parameters)
{
	if (!TempoServerEndpoint::IsUnixSocketTransportSupported())
	{
		return true;
	}

	// Short by construction: a path under the project's Saved dir can exceed sun_path.
	const FString SocketPath = FString::Printf(TEXT("/tmp/tempo-probe-%u.sock"), FPlatformProcess::GetCurrentProcessId());
	TempoServerEndpoint::RemoveSocketFile(SocketPath);

	TestFalse(TEXT("An unused path is not reported in use"), TempoServerEndpoint::IsSocketPathInUse(SocketPath));

#if PLATFORM_MAC || PLATFORM_LINUX
	// Stand up a real listener and confirm the probe sees it. This is the check that keeps a
	// second server from unlinking a running server's socket and taking its name.
	sockaddr_un Addr;
	FMemory::Memzero(&Addr, sizeof(Addr));
	Addr.sun_family = AF_UNIX;
	const FTCHARToUTF8 Utf8Path(*SocketPath);
	FMemory::Memcpy(Addr.sun_path, Utf8Path.Get(), Utf8Path.Length());

	const int32 Listener = socket(AF_UNIX, SOCK_STREAM, 0);
	if (Listener < 0)
	{
		AddError(TEXT("Could not create an AF_UNIX socket"));
		return true;
	}
	if (bind(Listener, reinterpret_cast<const sockaddr*>(&Addr), sizeof(Addr)) != 0 || listen(Listener, 1) != 0)
	{
		AddError(FString::Printf(TEXT("Could not listen on %s"), *SocketPath));
		close(Listener);
		TempoServerEndpoint::RemoveSocketFile(SocketPath);
		return true;
	}

	TestTrue(TEXT("The bound socket file exists"), SocketFileExists(SocketPath));
	TestTrue(TEXT("A live listener is reported in use"), TempoServerEndpoint::IsSocketPathInUse(SocketPath));

	// With the listener gone the file remains but nothing answers, which is the stale case gRPC
	// is allowed to clean up.
	close(Listener);
	TestFalse(TEXT("A stale socket file is not reported in use"), TempoServerEndpoint::IsSocketPathInUse(SocketPath));
	TestTrue(TEXT("The stale socket file is still there"), SocketFileExists(SocketPath));

	TempoServerEndpoint::RemoveSocketFile(SocketPath);
	TestFalse(TEXT("RemoveSocketFile removes the socket"), SocketFileExists(SocketPath));
#endif

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
