// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreBootstrap.h"

#include "Interfaces/IPluginManager.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	// Unreal's own Ctrl-C handler, which it installs only when the process has a console window.
	// Under a service (a CI runner) the process has a console with no window, so Unreal skips its
	// handler. A Ctrl-C (how a runner cancels a step) then reaches Windows' default handler,
	// which calls ExitProcess. ExitProcess kills every other thread and runs each DLL's atexit
	// handlers on the one left. A handler that logs then waits forever for the log writer thread,
	// which is already dead. A process that has begun ExitProcess also refuses TerminateProcess
	// from outside, so not even taskkill /F stops it, and it holds its files until a reboot.
	// Unreal's handler hard-terminates (no atexit handlers) for an unattended process or a
	// non-Ctrl-C event, and CallGracefulTerminationHandler runs exactly that path: request exit,
	// broadcast ApplicationWillTerminate, flush the logs, TerminateProcess.
	BOOL WINAPI TempoConsoleCtrlHandler(DWORD /*CtrlType*/)
	{
		FWindowsPlatformMisc::CallGracefulTerminationHandler();
		return true;
	}
}
#endif

void FTempoCoreBootstrapModule::StartupModule()
{
#if PLATFORM_WINDOWS
	// Exactly when Unreal skipped its own handler (FWindowsPlatformMisc::SetGracefulTerminationHandler),
	// so it never displaces Unreal's interactive two-step Ctrl-C.
	if (GetConsoleWindow() == nullptr)
	{
		SetConsoleCtrlHandler(TempoConsoleCtrlHandler, true);
	}
#endif

#if PLATFORM_WINDOWS && !IS_MONOLITHIC
	// TempoCore.dll imports tempogrpc.dll, which Windows looks for beside the executable, not beside
	// the importing DLL. Unreal resolves a module's imports from its registered DLL directories
	// first, so register the one the TempoThirdParty release puts tempogrpc.dll in.
	const TSharedPtr<IPlugin> TempoCorePlugin = IPluginManager::Get().FindPlugin(TEXT("TempoCore"));
	if (TempoCorePlugin.IsValid())
	{
		FPlatformProcess::AddDllDirectory(*FPaths::Combine(TempoCorePlugin->GetBaseDir(), TEXT("Source"), TEXT("ThirdParty"), TEXT("gRPC"), TEXT("Binaries"), TEXT("Windows")));
	}
#endif
}

IMPLEMENT_MODULE(FTempoCoreBootstrapModule, TempoCoreBootstrap)
