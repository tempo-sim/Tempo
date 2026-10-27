// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoEngineServiceSubsystem.h"

#include "TempoCore/Engine.grpc.pb.h"
#if PLATFORM_WINDOWS
// gRPC transitively includes <windows.h>, which leaks wingdi.h's GetObject macro
// (GetObjectA/W). In TempoCore's unity TU, the next .cpp file's transitive include
// of WheeledVehiclePawn.h -> Chaos's ImplicitObjectScaled.h would otherwise see
// `Implicit.template GetObject<T>()` mangled to `GetObjectW` — not a member of
// FImplicitObject. Scrub the macro here so later parses are clean.
#undef GetObject
#endif

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/StringOutputDevice.h"

using EngineService = TempoCore::EngineService;
using EngineAsyncService = TempoCore::EngineService::AsyncService;
using SetConsoleVariableRequest = TempoCore::SetConsoleVariableRequest;
using GetConsoleVariableRequest = TempoCore::GetConsoleVariableRequest;
using ConsoleVariableResponse = TempoCore::ConsoleVariableResponse;
using ExecConsoleCommandRequest = TempoCore::ExecConsoleCommandRequest;
using ExecConsoleCommandResponse = TempoCore::ExecConsoleCommandResponse;
using EngineInfoResponse = TempoCore::EngineInfoResponse;

namespace
{
	IConsoleVariable* FindConsoleVariable(const std::string& Name, grpc::Status& OutStatus)
	{
		if (Name.empty())
		{
			OutStatus = grpc::Status(grpc::INVALID_ARGUMENT, "Console variable name must not be empty");
			return nullptr;
		}

		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(UTF8_TO_TCHAR(Name.c_str()), false);
		if (!CVar)
		{
			OutStatus = grpc::Status(grpc::NOT_FOUND, "No console variable named " + Name);
			return nullptr;
		}

		if (!CVar->IsEnabled())
		{
			OutStatus = grpc::Status(grpc::FAILED_PRECONDITION, "Console variable " + Name + " is disabled in this build (cheat variables are disabled in Shipping)");
			return nullptr;
		}

		OutStatus = grpc::Status_OK;
		return CVar;
	}

	// The world a console command typed now would run in: the game's, when one is being played,
	// otherwise the editor's.
	UWorld* GetConsoleWorld()
	{
		UWorld* EditorWorld = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World)
			{
				continue;
			}
			if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
			{
				return World;
			}
			if (Context.WorldType == EWorldType::Editor)
			{
				EditorWorld = World;
			}
		}
		return EditorWorld;
	}
}

void UTempoEngineServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<EngineService>(
		SimpleRequestHandler(&EngineAsyncService::RequestSetConsoleVariable, &UTempoEngineServiceSubsystem::SetConsoleVariable),
		SimpleRequestHandler(&EngineAsyncService::RequestGetConsoleVariable, &UTempoEngineServiceSubsystem::GetConsoleVariable),
		SimpleRequestHandler(&EngineAsyncService::RequestExecConsoleCommand, &UTempoEngineServiceSubsystem::ExecConsoleCommand),
		SimpleRequestHandler(&EngineAsyncService::RequestGetEngineInfo, &UTempoEngineServiceSubsystem::GetEngineInfo)
	);
}

void UTempoEngineServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<EngineService>(this);
}

void UTempoEngineServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<EngineService>();
}

void UTempoEngineServiceSubsystem::SetConsoleVariable(const SetConsoleVariableRequest& Request, const TResponseDelegate<ConsoleVariableResponse>& ResponseContinuation) const
{
	grpc::Status Status;
	IConsoleVariable* CVar = FindConsoleVariable(Request.name(), Status);
	if (!CVar)
	{
		ResponseContinuation.ExecuteIfBound(ConsoleVariableResponse(), Status);
		return;
	}

	if (CVar->TestFlags(ECVF_ReadOnly))
	{
		ResponseContinuation.ExecuteIfBound(ConsoleVariableResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Console variable " + Request.name() + " is read-only"));
		return;
	}

	const FString Value = FString(UTF8_TO_TCHAR(Request.value().c_str())).TrimStartAndEnd();

	// The console parses a malformed number as zero without complaint. Refuse it instead.
	if ((CVar->IsVariableInt() || CVar->IsVariableFloat()) && !FCString::IsNumeric(*Value))
	{
		ResponseContinuation.ExecuteIfBound(ConsoleVariableResponse(), grpc::Status(grpc::INVALID_ARGUMENT, "Console variable " + Request.name() + " is numeric, but the value '" + Request.value() + "' is not a number"));
		return;
	}

	// The priority the interactive console uses, so the value is not silently ignored for having
	// been set more strongly elsewhere (e.g. on the command line).
	CVar->Set(*Value, ECVF_SetByConsole);

	ConsoleVariableResponse Response;
	Response.set_value(TCHAR_TO_UTF8(*CVar->GetString()));
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoEngineServiceSubsystem::GetConsoleVariable(const GetConsoleVariableRequest& Request, const TResponseDelegate<ConsoleVariableResponse>& ResponseContinuation) const
{
	grpc::Status Status;
	const IConsoleVariable* CVar = FindConsoleVariable(Request.name(), Status);
	if (!CVar)
	{
		ResponseContinuation.ExecuteIfBound(ConsoleVariableResponse(), Status);
		return;
	}

	ConsoleVariableResponse Response;
	Response.set_value(TCHAR_TO_UTF8(*CVar->GetString()));
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoEngineServiceSubsystem::ExecConsoleCommand(const ExecConsoleCommandRequest& Request, const TResponseDelegate<ExecConsoleCommandResponse>& ResponseContinuation) const
{
	const FString Command = FString(UTF8_TO_TCHAR(Request.command().c_str())).TrimStartAndEnd();
	if (Command.IsEmpty())
	{
		ResponseContinuation.ExecuteIfBound(ExecConsoleCommandResponse(), grpc::Status(grpc::INVALID_ARGUMENT, "Console command must not be empty"));
		return;
	}

	UWorld* World = GetConsoleWorld();
	FStringOutputDevice Output;
	Output.SetAutoEmitLineTerminator(true);

	// The local player is where the in-game console sends a command: it offers it to the player, the
	// viewport, the player controller and finally the engine. Without one, the engine (in the editor,
	// the editor engine) is the whole chain.
	ULocalPlayer* LocalPlayer = World ? World->GetFirstLocalPlayerFromController() : nullptr;
	const bool bHandled = LocalPlayer ? LocalPlayer->Exec(World, *Command, Output) : GEngine->Exec(World, *Command, Output);
	if (!bHandled)
	{
		ResponseContinuation.ExecuteIfBound(ExecConsoleCommandResponse(), grpc::Status(grpc::NOT_FOUND, "Console command not recognized: " + Request.command()));
		return;
	}

	ExecConsoleCommandResponse Response;
	Response.set_output(TCHAR_TO_UTF8(*Output));
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoEngineServiceSubsystem::GetEngineInfo(const TempoCore::Empty& Request, const TResponseDelegate<EngineInfoResponse>& ResponseContinuation) const
{
	EngineInfoResponse Response;
	Response.set_engine_version(TCHAR_TO_UTF8(*FEngineVersion::Current().ToString()));
	Response.set_project_name(TCHAR_TO_UTF8(FApp::GetProjectName()));
	Response.set_build_configuration(TCHAR_TO_UTF8(LexToString(FApp::GetBuildConfiguration())));
	Response.set_editor(GIsEditor);
	Response.set_platform(TCHAR_TO_UTF8(*FString(FPlatformProperties::IniPlatformName())));
	Response.set_can_render(FApp::CanEverRender());
	Response.set_rhi(TCHAR_TO_UTF8(*FApp::GetGraphicsRHI()));
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}
