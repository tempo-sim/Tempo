// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreServiceSubsystem.h"

#include "TempoGameMode.h"

#include "TempoCore/TempoCore.grpc.pb.h"

#include "GameFramework/GameMode.h"
#include "Kismet/GameplayStatics.h"

using TempoCoreService = TempoCore::TempoCoreService;
using TempoCoreAsyncService = TempoCore::TempoCoreService::AsyncService;
using LoadLevelRequest = TempoCore::LoadLevelRequest;

void UTempoCoreServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<TempoCoreService>(
		SimpleRequestHandler(&TempoCoreAsyncService::RequestLoadLevel, &UTempoCoreServiceSubsystem::LoadLevel),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestFinishLoadingLevel, &UTempoCoreServiceSubsystem::FinishLoadingLevel),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestQuit, &UTempoCoreServiceSubsystem::Quit)
	);
}

void UTempoCoreServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().ActivateService<TempoCoreService>(this);
}

void UTempoCoreServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<TempoCoreService>();
}

void UTempoCoreServiceSubsystem::LoadLevel(const LoadLevelRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (Request.deferred())
	{
		const ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
		if (!TempoGameMode)
		{
			ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Deferred level loading is only supported when using TempoGameMode"));
			return;
		}
		SetDeferBeginPlay(true);
	}

	if (Request.start_paused())
	{
		const ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
		if (!TempoGameMode)
		{
			ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Starting paused is only supported when using TempoGameMode"));
			return;
		}
		SetStartPaused(true);
	}

	UGameplayStatics::OpenLevel(this, FName(UTF8_TO_TCHAR(Request.level().c_str())));

	PendingLevelLoad = ResponseContinuation;
}

void UTempoCoreServiceSubsystem::OnLevelLoaded()
{
	if (PendingLevelLoad.IsSet())
	{
		PendingLevelLoad->ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
	}
}

void UTempoCoreServiceSubsystem::FinishLoadingLevel(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
	if (!TempoGameMode)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Deferred level loading is only supported when using TempoGameMode"));
		return;
	}
	if (!TempoGameMode->BeginPlayDeferred())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "No deferred level load found"));
		return;
	}

	SetDeferBeginPlay(false);
	TempoGameMode->StartPlay();
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoCoreServiceSubsystem::Quit(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	RequestEngineExit(TEXT("TempoCore API received quit request"));

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}
