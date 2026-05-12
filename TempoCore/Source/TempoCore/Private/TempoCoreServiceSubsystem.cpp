// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreServiceSubsystem.h"

#include "TempoCore.h"
#include "TempoCoreSettings.h"
#include "TempoGameMode.h"

#include "TempoCore/TempoCore.grpc.pb.h"
#if PLATFORM_WINDOWS
// gRPC transitively includes <windows.h>, which leaks wingdi.h's GetObject macro
// (GetObjectA/W). In TempoCore's unity TU, the next .cpp file's transitive include
// of WheeledVehiclePawn.h -> Chaos's ImplicitObjectScaled.h would otherwise see
// `Implicit.template GetObject<T>()` mangled to `GetObjectW` — not a member of
// FImplicitObject. Scrub the macro here so later parses are clean.
#undef GetObject
#endif

#include "GameFramework/GameMode.h"
#include "Kismet/GameplayStatics.h"

using TempoCoreService = TempoCore::TempoCoreService;
using TempoCoreAsyncService = TempoCore::TempoCoreService::AsyncService;
using LoadLevelRequest = TempoCore::LoadLevelRequest;
using CurrentLevelResponse = TempoCore::CurrentLevelResponse;
using SetMainViewportRenderEnabledRequest = TempoCore::SetMainViewportRenderEnabledRequest;
using SetControlModeRequest = TempoCore::SetControlModeRequest;

void UTempoCoreServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<TempoCoreService>(
		SimpleRequestHandler(&TempoCoreAsyncService::RequestLoadLevel, &UTempoCoreServiceSubsystem::LoadLevel),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestFinishLoadingLevel, &UTempoCoreServiceSubsystem::FinishLoadingLevel),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestGetCurrentLevelName, &UTempoCoreServiceSubsystem::GetCurrentLevelName),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestQuit, &UTempoCoreServiceSubsystem::Quit),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestSetMainViewportRenderEnabled, &UTempoCoreServiceSubsystem::SetRenderMainViewportEnabled),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestSetControlMode, &UTempoCoreServiceSubsystem::SetControlMode)
	);
}

void UTempoCoreServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<TempoCoreService>(this);
}

void UTempoCoreServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<TempoCoreService>();
}

void UTempoCoreServiceSubsystem::LoadLevel(const LoadLevelRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	if (Request.deferred())
	{
		const ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
		if (!TempoGameMode)
		{
			ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, "Deferred level loading is only supported when using TempoGameMode"));
			return;
		}
		SetDeferBeginPlay(true);
	}

	if (Request.start_paused())
	{
		const ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
		if (!TempoGameMode)
		{
			ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, "Starting paused is only supported when using TempoGameMode"));
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
		PendingLevelLoad->ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
	}
}

void UTempoCoreServiceSubsystem::FinishLoadingLevel(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
	if (!TempoGameMode)
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, "Deferred level loading is only supported when using TempoGameMode"));
		return;
	}
	if (!TempoGameMode->BeginPlayDeferred())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "No deferred level load found"));
		return;
	}

	SetDeferBeginPlay(false);
	TempoGameMode->StartPlay();
	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoCoreServiceSubsystem::GetCurrentLevelName(const TempoCore::Empty& Request, const TResponseDelegate<CurrentLevelResponse>& ResponseContinuation)
{
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(this);

	CurrentLevelResponse Response;
	Response.set_level(TCHAR_TO_UTF8(*CurrentLevelName));
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoCoreServiceSubsystem::Quit(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	RequestEngineExit(TEXT("TempoCore API received quit request"));

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoCoreServiceSubsystem::SetRenderMainViewportEnabled(const SetMainViewportRenderEnabledRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	if (UTempoCoreSettings* TempoCoreSettings = GetMutableDefault<UTempoCoreSettings>())
	{
		TempoCoreSettings->SetRenderMainViewport(Request.enabled());
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Could not find TempoCoreSettings"));
}

void UTempoCoreServiceSubsystem::SetControlMode(const SetControlModeRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	const ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
	if (!TempoGameMode)
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, "Setting control mode is only supported when using TempoGameMode"));
		return;
	}

	EControlMode ControlMode;
	switch (Request.mode())
	{
	case TempoCore::CM_DISABLED:
		{
			ControlMode = EControlMode::None;
			break;
		}
	case TempoCore::CM_USER:
		{
			ControlMode = EControlMode::User;
			break;
		}
	case TempoCore::CM_OPEN_LOOP:
		{
			ControlMode = EControlMode::OpenLoop;
			break;
		}
	case TempoCore::CM_CLOSED_LOOP:
		{
			ControlMode = EControlMode::ClosedLoop;
			break;
		}
	default:
		{
			UE_LOG(LogTempoCore, Error, TEXT("Unhandled control mode in UTempoCoreServiceSubsystem::SetControlMode"));
			const FString ErrorMsg = FString::Printf(TEXT("Unhandled ControlMode: %d"), Request.mode());
			ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, TCHAR_TO_UTF8(*ErrorMsg)));
			return;
		}
	}

	if (FString ErrorMsg; !TempoGameMode->SetControlMode(ControlMode, ErrorMsg))
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, TCHAR_TO_UTF8(*ErrorMsg)));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}
