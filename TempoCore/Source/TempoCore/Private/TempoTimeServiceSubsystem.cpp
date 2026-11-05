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
using SetStepsPerGameFrameRequest = TempoCore::SetStepsPerGameFrameRequest;
using AdvanceStepsRequest = TempoCore::AdvanceStepsRequest;
using StepResponse = TempoCore::StepResponse;
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

	// Steps NumSteps and responds with where the last step left the game frame, once it completes.
	void StepAndRespond(ATempoWorldSettings* WorldSettings, int32 NumSteps, const TResponseDelegate<StepResponse>& ResponseContinuation)
	{
		const TResponseDelegate<StepResponse> Continuation = ResponseContinuation;
		const TWeakObjectPtr<ATempoWorldSettings> WeakWorldSettings(WorldSettings);
		const bool bAccepted = WorldSettings->Step(NumSteps, [Continuation, WeakWorldSettings](ETempoStepResult Result)
		{
			StepResponse Response;
			if (const ATempoWorldSettings* Settings = WeakWorldSettings.Get())
			{
				Response.set_game_frame(Settings->GetStepsIntoGameFrame() == 0);
				Response.set_steps_until_game_frame(Settings->GetStepsUntilGameFrame());
			}
			Continuation.ExecuteIfBound(Response, StepResultToStatus(Result));
		});

		if (!bAccepted)
		{
			ResponseContinuation.ExecuteIfBound(StepResponse(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Another step is already in progress"));
		}
	}
}

void UTempoTimeServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<TimeService>(
		SimpleRequestHandler(&TimeAsyncService::RequestSetTimeMode, &UTempoTimeServiceSubsystem::SetTimeMode).GameFrameOnly(),
		SimpleRequestHandler(&TimeAsyncService::RequestSetSimStepsPerSecond, &UTempoTimeServiceSubsystem::SetSimStepsPerSecond).GameFrameOnly(),
		SimpleRequestHandler(&TimeAsyncService::RequestSetStepsPerGameFrame, &UTempoTimeServiceSubsystem::SetStepsPerGameFrame).GameFrameOnly(),
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
	if (ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings());
		WorldSettings && UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		WorldSettings->Play();
	}
	else if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
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

void UTempoTimeServiceSubsystem::SetStepsPerGameFrame(const SetStepsPerGameFrameRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	const int32 RequestedStepsPerGameFrame = Request.steps_per_game_frame();

	if (RequestedStepsPerGameFrame < 1)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::OUT_OF_RANGE, "Steps per game frame must be >= 1"));
		return;
	}

	if (!Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "SetStepsPerGameFrame has no effect unless using TempoWorldSettings"));
		return;
	}

	GetMutableDefault<UTempoCoreSettings>()->SetStepsPerGameFrame(RequestedStepsPerGameFrame);

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}

void UTempoTimeServiceSubsystem::AdvanceSteps(const AdvanceStepsRequest& Request, const TResponseDelegate<StepResponse>& ResponseContinuation) const
{
	const int32 RequestedSteps = Request.steps();

	if (RequestedSteps < 1)
	{
		ResponseContinuation.ExecuteIfBound(StepResponse(), grpc::Status(grpc::StatusCode::OUT_OF_RANGE, "Steps must be >= 1"));
		return;
	}

	ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings());
	if (!WorldSettings)
	{
		ResponseContinuation.ExecuteIfBound(StepResponse(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "AdvanceSteps is only supported when using TempoWorldSettings"));
		return;
	}

	StepAndRespond(WorldSettings, RequestedSteps, ResponseContinuation);
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

void UTempoTimeServiceSubsystem::Step(const TempoEmpty& Request, const TResponseDelegate<StepResponse>& ResponseContinuation) const
{
	ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings());
	if (!WorldSettings)
	{
		ResponseContinuation.ExecuteIfBound(StepResponse(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Step is only supported when using TempoWorldSettings"));
		return;
	}

	StepAndRespond(WorldSettings, 1, ResponseContinuation);
}

void UTempoTimeServiceSubsystem::GetSimTime(const TempoEmpty& Request, const TResponseDelegate<GetSimTimeResponse>& ResponseContinuation) const
{
	GetSimTimeResponse Response;
	Response.set_sim_time(GetWorld()->GetTimeSeconds());
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}
