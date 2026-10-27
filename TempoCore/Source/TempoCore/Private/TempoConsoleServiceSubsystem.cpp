// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoConsoleServiceSubsystem.h"

#include "TempoCore/Console.grpc.pb.h"
#if PLATFORM_WINDOWS
// gRPC transitively includes <windows.h>, which leaks wingdi.h's GetObject macro
// (GetObjectA/W). In TempoCore's unity TU, the next .cpp file's transitive include
// of WheeledVehiclePawn.h -> Chaos's ImplicitObjectScaled.h would otherwise see
// `Implicit.template GetObject<T>()` mangled to `GetObjectW` — not a member of
// FImplicitObject. Scrub the macro here so later parses are clean.
#undef GetObject
#endif

#include "HAL/IConsoleManager.h"

using ConsoleService = TempoCore::ConsoleService;
using ConsoleAsyncService = TempoCore::ConsoleService::AsyncService;
using SetConsoleVariableRequest = TempoCore::SetConsoleVariableRequest;
using GetConsoleVariableRequest = TempoCore::GetConsoleVariableRequest;
using ConsoleVariableResponse = TempoCore::ConsoleVariableResponse;

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
}

void UTempoConsoleServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<ConsoleService>(
		SimpleRequestHandler(&ConsoleAsyncService::RequestSetConsoleVariable, &UTempoConsoleServiceSubsystem::SetConsoleVariable),
		SimpleRequestHandler(&ConsoleAsyncService::RequestGetConsoleVariable, &UTempoConsoleServiceSubsystem::GetConsoleVariable)
	);
}

void UTempoConsoleServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<ConsoleService>(this);
}

void UTempoConsoleServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<ConsoleService>();
}

void UTempoConsoleServiceSubsystem::SetConsoleVariable(const SetConsoleVariableRequest& Request, const TResponseDelegate<ConsoleVariableResponse>& ResponseContinuation) const
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

void UTempoConsoleServiceSubsystem::GetConsoleVariable(const GetConsoleVariableRequest& Request, const TResponseDelegate<ConsoleVariableResponse>& ResponseContinuation) const
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
