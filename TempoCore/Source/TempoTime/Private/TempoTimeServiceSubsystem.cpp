// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTimeServiceSubsystem.h"

#include "TempoCoreSettings.h"
#include "TempoTimeWorldSettings.h"
#include "TempoTime/Time.grpc.pb.h"

#include "Kismet/GameplayStatics.h"

using TimeService = TempoTime::TimeService;
using TimeAsyncService = TempoTime::TimeService::AsyncService;
using TempoEmpty = TempoScripting::Empty;
using TimeModeRequest = TempoTime::TimeModeRequest;
using SetSimStepsPerSecondRequest = TempoTime::SetSimStepsPerSecondRequest;
using AdvanceStepsRequest = TempoTime::AdvanceStepsRequest;

void UTempoTimeServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<TimeService>(
		SimpleRequestHandler(&TimeAsyncService::RequestSetTimeMode, &UTempoTimeServiceSubsystem::SetTimeMode),
		SimpleRequestHandler(&TimeAsyncService::RequestSetSimStepsPerSecond, &UTempoTimeServiceSubsystem::SetSimStepsPerSecond),
		SimpleRequestHandler(&TimeAsyncService::RequestAdvanceSteps, &UTempoTimeServiceSubsystem::AdvanceSteps),
		SimpleRequestHandler(&TimeAsyncService::RequestPlay, &UTempoTimeServiceSubsystem::Play),
		SimpleRequestHandler(&TimeAsyncService::RequestPause, &UTempoTimeServiceSubsystem::Pause),
		SimpleRequestHandler(&TimeAsyncService::RequestStep, &UTempoTimeServiceSubsystem::Step)
		);
}

void UTempoTimeServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	FTempoScriptingServer::Get().ActivateService<TimeService>(this);
}

void UTempoTimeServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<TimeService>();
}

void UTempoTimeServiceSubsystem::SetTimeMode(const TempoTime::TimeModeRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	if (!Cast<ATempoTimeWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "SetTimeMode has no effect unless using TempoTimeWorldSettings"));
		return;
	}

	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
	switch (Request.time_mode())
	{
	case TempoTime::FIXED_STEP:
		{
			Settings->SetTimeMode(ETimeMode::FixedStep);
			break;
		}
	case TempoTime::WALL_CLOCK:
		{
			Settings->SetTimeMode(ETimeMode::WallClock);
			break;
		}
	default:
		{
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::UNKNOWN, "Unrecognized time mode"));
			return;
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}

void UTempoTimeServiceSubsystem::SetSimStepsPerSecond(const SetSimStepsPerSecondRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	const int32 RequestedStepsPerSecond = Request.sim_steps_per_second();

	if (RequestedStepsPerSecond < 1)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::OUT_OF_RANGE, "Steps per second must be >= 1"));
		return;
	}

	if (!Cast<ATempoTimeWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "SetSimStepsPerSecond has no effect unless using TempoTimeWorldSettings"));
		return;
	}

	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
	Settings->SetSimulatedStepsPerSecond(RequestedStepsPerSecond);

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}

void UTempoTimeServiceSubsystem::Play(const TempoEmpty& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PlayerController->SetPause(false);
	}
	else
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Failed to unpause (could not find player controller)"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}

void UTempoTimeServiceSubsystem::AdvanceSteps(const AdvanceStepsRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	const int32 RequestedSteps = Request.steps();

	if (RequestedSteps < 1)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::OUT_OF_RANGE, "Steps must be >= 1"));
		return;
	}
	
	if (ATempoTimeWorldSettings* WorldSettings = Cast<ATempoTimeWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		WorldSettings->Step(RequestedSteps);
	}
	else
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "AdvanceSteps is only supported when using TempoTimeWorldSettings"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status());
}


void UTempoTimeServiceSubsystem::Pause(const TempoEmpty& Request, const TDelegate<void(const TempoEmpty&, grpc::Status)>& ResponseContinuation) const
{
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PlayerController->SetPause(true);
	}
	else
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Failed to pause (could not find player controller)"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status());
}

void UTempoTimeServiceSubsystem::Step(const TempoEmpty& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	if (ATempoTimeWorldSettings* WorldSettings = Cast<ATempoTimeWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		WorldSettings->Step(1);
	}
	else
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Step is only supported when using TempoTimeWorldSettings"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status());
}
