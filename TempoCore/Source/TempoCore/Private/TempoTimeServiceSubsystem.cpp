// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTimeServiceSubsystem.h"

#include "TempoCoreSettings.h"
#include "TempoWorldSettings.h"
#include "TempoCore/Time.grpc.pb.h"
#if PLATFORM_WINDOWS
// gRPC transitively includes <windows.h>, which leaks wingdi.h's GetObject macro
// (GetObjectA/W). In TempoCore's unity TU, the next .cpp file's transitive include
// of WheeledVehiclePawn.h -> Chaos's ImplicitObjectScaled.h would otherwise see
// `Implicit.template GetObject<T>()` mangled to `GetObjectW` — not a member of
// FImplicitObject. Scrub the macro here so later parses are clean.
#undef GetObject
#endif

#include "Kismet/GameplayStatics.h"

using TimeService = TempoCore::TimeService;
using TimeAsyncService = TempoCore::TimeService::AsyncService;
using TempoEmpty = TempoCore::Empty;
using TimeModeRequest = TempoCore::TimeModeRequest;
using SetSimStepsPerSecondRequest = TempoCore::SetSimStepsPerSecondRequest;
using AdvanceStepsRequest = TempoCore::AdvanceStepsRequest;
using GetSimTimeResponse = TempoCore::GetSimTimeResponse;

namespace
{
	grpc::Status StepResultToStatus(ETempoStepResult Result)
	{
		switch (Result)
		{
		case ETempoStepResult::Completed:
			return grpc::Status_OK;
		case ETempoStepResult::AbortedByExternalPause:
			return grpc::Status(grpc::StatusCode::ABORTED, "Step was aborted by an external pause or unpause");
		case ETempoStepResult::AbortedByTimeSettingsChanged:
			return grpc::Status(grpc::StatusCode::ABORTED, "Step was aborted by a time settings change");
		}
		return grpc::Status(grpc::StatusCode::UNKNOWN, "Step ended with an unknown result");
	}
}

void UTempoTimeServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<TimeService>(
		SimpleRequestHandler(&TimeAsyncService::RequestSetTimeMode, &UTempoTimeServiceSubsystem::SetTimeMode),
		SimpleRequestHandler(&TimeAsyncService::RequestSetSimStepsPerSecond, &UTempoTimeServiceSubsystem::SetSimStepsPerSecond),
		SimpleRequestHandler(&TimeAsyncService::RequestAdvanceSteps, &UTempoTimeServiceSubsystem::AdvanceSteps),
		SimpleRequestHandler(&TimeAsyncService::RequestPlay, &UTempoTimeServiceSubsystem::Play),
		SimpleRequestHandler(&TimeAsyncService::RequestPause, &UTempoTimeServiceSubsystem::Pause),
		SimpleRequestHandler(&TimeAsyncService::RequestStep, &UTempoTimeServiceSubsystem::Step),
		SimpleRequestHandler(&TimeAsyncService::RequestGetSimTime, &UTempoTimeServiceSubsystem::GetSimTime)
		);
}

void UTempoTimeServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<TimeService>(this);
}

void UTempoTimeServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<TimeService>();
}

void UTempoTimeServiceSubsystem::SetTimeMode(const TempoCore::TimeModeRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	if (!Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "SetTimeMode has no effect unless using TempoWorldSettings"));
		return;
	}

	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
	switch (Request.time_mode())
	{
	case TempoCore::TM_FIXED_STEP:
		{
			Settings->SetTimeMode(ETimeMode::FixedStep);
			break;
		}
	case TempoCore::TM_WALL_CLOCK:
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

	if (!Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "SetSimStepsPerSecond has no effect unless using TempoWorldSettings"));
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

	ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings());
	if (!WorldSettings)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "AdvanceSteps is only supported when using TempoWorldSettings"));
		return;
	}

	const TResponseDelegate<TempoEmpty> Continuation = ResponseContinuation;
	const bool bAccepted = WorldSettings->Step(RequestedSteps, [Continuation](ETempoStepResult Result)
	{
		Continuation.ExecuteIfBound(TempoEmpty(), StepResultToStatus(Result));
	});

	if (!bAccepted)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Another step is already in progress"));
	}
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
	ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings());
	if (!WorldSettings)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Step is only supported when using TempoWorldSettings"));
		return;
	}

	const TResponseDelegate<TempoEmpty> Continuation = ResponseContinuation;
	const bool bAccepted = WorldSettings->Step(1, [Continuation](ETempoStepResult Result)
	{
		Continuation.ExecuteIfBound(TempoEmpty(), StepResultToStatus(Result));
	});

	if (!bAccepted)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Another step is already in progress"));
	}
}

void UTempoTimeServiceSubsystem::GetSimTime(const TempoEmpty& Request, const TResponseDelegate<GetSimTimeResponse>& ResponseContinuation) const
{
	GetSimTimeResponse Response;
	Response.set_sim_time(GetWorld()->GetTimeSeconds());
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}
