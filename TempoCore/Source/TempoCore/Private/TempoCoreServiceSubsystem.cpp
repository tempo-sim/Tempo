// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreServiceSubsystem.h"

#include "TempoCore.h"
#include "TempoCoreSettings.h"
#include "TempoGameMode.h"

#include "TempoCore/TempoCore.grpc.pb.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "GameFramework/GameMode.h"
#include "Kismet/GameplayStatics.h"

using TempoCoreService = TempoCore::TempoCoreService;
using TempoCoreAsyncService = TempoCore::TempoCoreService::AsyncService;
using LoadLevelRequest = TempoCore::LoadLevelRequest;
using CurrentLevelResponse = TempoCore::CurrentLevelResponse;
using GetAvailableLevelsRequest = TempoCore::GetAvailableLevelsRequest;
using AvailableLevelsResponse = TempoCore::AvailableLevelsResponse;
using SetMainViewportRenderEnabledRequest = TempoCore::SetMainViewportRenderEnabledRequest;
using SetControlModeRequest = TempoCore::SetControlModeRequest;

void UTempoCoreServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<TempoCoreService>(
		SimpleRequestHandler(&TempoCoreAsyncService::RequestLoadLevel, &UTempoCoreServiceSubsystem::LoadLevel),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestFinishLoadingLevel, &UTempoCoreServiceSubsystem::FinishLoadingLevel),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestGetCurrentLevelName, &UTempoCoreServiceSubsystem::GetCurrentLevelName),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestGetAvailableLevels, &UTempoCoreServiceSubsystem::GetAvailableLevels),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestQuit, &UTempoCoreServiceSubsystem::Quit),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestSetMainViewportRenderEnabled, &UTempoCoreServiceSubsystem::SetRenderMainViewportEnabled),
		SimpleRequestHandler(&TempoCoreAsyncService::RequestSetControlMode, &UTempoCoreServiceSubsystem::SetControlMode)
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

void UTempoCoreServiceSubsystem::GetCurrentLevelName(const TempoScripting::Empty& Request, const TResponseDelegate<CurrentLevelResponse>& ResponseContinuation)
{
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(this);

	CurrentLevelResponse Response;
	Response.set_level(TCHAR_TO_UTF8(*CurrentLevelName));
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoCoreServiceSubsystem::GetAvailableLevels(const GetAvailableLevelsRequest& Request, const TResponseDelegate<AvailableLevelsResponse>& ResponseContinuation) const
{
	AvailableLevelsResponse Response;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// Use provided search path or default to /Game/
	FString SearchPath = UTF8_TO_TCHAR(Request.search_path().c_str());
	if (SearchPath.IsEmpty())
	{
		SearchPath = TEXT("/Game");
	}

	FARFilter Filter;
	Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(*SearchPath);
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	for (const FAssetData& Asset : AssetList)
	{
		FString LevelPath = Asset.PackageName.ToString();
		Response.add_levels(TCHAR_TO_UTF8(*LevelPath));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoCoreServiceSubsystem::Quit(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	RequestEngineExit(TEXT("TempoCore API received quit request"));

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoCoreServiceSubsystem::SetRenderMainViewportEnabled(const SetMainViewportRenderEnabledRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (UTempoCoreSettings* TempoCoreSettings = GetMutableDefault<UTempoCoreSettings>())
	{
		TempoCoreSettings->SetRenderMainViewport(Request.enabled());
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Could not find TempoCoreSettings"));
}

void UTempoCoreServiceSubsystem::SetControlMode(const SetControlModeRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	const ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
	if (!TempoGameMode)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Setting control mode is only supported when using TempoGameMode"));
		return;
	}

	EControlMode ControlMode;
	switch (Request.mode())
	{
	case TempoCore::NONE:
		{
			ControlMode = EControlMode::None;
			break;
		}
	case TempoCore::USER:
		{
			ControlMode = EControlMode::User;
			break;
		}
	case TempoCore::OPEN_LOOP:
		{
			ControlMode = EControlMode::OpenLoop;
			break;
		}
	case TempoCore::CLOSED_LOOP:
		{
			ControlMode = EControlMode::ClosedLoop;
			break;
		}
	default:
		{
			UE_LOG(LogTempoCore, Error, TEXT("Unhandled control mode in UTempoCoreServiceSubsystem::SetControlMode"));
			const FString ErrorMsg = FString::Printf(TEXT("Unhandled ControlMode: %d"), Request.mode());
			ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, TCHAR_TO_UTF8(*ErrorMsg)));
			return;
		}
	}

	if (FString ErrorMsg; !TempoGameMode->SetControlMode(ControlMode, ErrorMsg))
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, TCHAR_TO_UTF8(*ErrorMsg)));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}
