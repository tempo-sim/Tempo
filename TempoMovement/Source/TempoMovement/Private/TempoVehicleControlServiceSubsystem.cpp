// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoVehicleControlServiceSubsystem.h"

#include "TempoVehicleControlInterface.h"
#include "TempoDroneControlInterface.h"
#include "TempoMovement/VehicleControlService.grpc.pb.h"
#include "TempoScripting/Empty.pb.h"

#include "Kismet/GameplayStatics.h"

using VehicleControlService = TempoMovement::VehicleControlService;
using VehicleControlAsyncService = TempoMovement::VehicleControlService::AsyncService;
using VehicleCommandRequest = TempoMovement::VehicleCommandRequest;
using DroneCommandRequest = TempoMovement::DroneCommandRequest;
using CommandableVehiclesResponse = TempoMovement::CommandableVehiclesResponse;
using TempoEmpty = TempoScripting::Empty;

void UTempoVehicleControlServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<VehicleControlService>(
		SimpleRequestHandler(&VehicleControlAsyncService::RequestCommandVehicle, &UTempoVehicleControlServiceSubsystem::HandleVehicleCommand),
		SimpleRequestHandler(&VehicleControlAsyncService::RequestCommandDrone, &UTempoVehicleControlServiceSubsystem::HandleDroneCommand),
		SimpleRequestHandler(&VehicleControlAsyncService::RequestGetCommandableVehicles, &UTempoVehicleControlServiceSubsystem::GetCommandableVehicles)
		);
}

void UTempoVehicleControlServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().ActivateService<VehicleControlService>(this);
}

void UTempoVehicleControlServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<VehicleControlService>();
}

void UTempoVehicleControlServiceSubsystem::GetCommandableVehicles(const TempoScripting::Empty& Request, const TResponseDelegate<TempoMovement::CommandableVehiclesResponse>& ResponseContinuation) const
{
	CommandableVehiclesResponse Response;

	TArray<AActor*> VehicleControllers;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UTempoVehicleControlInterface::StaticClass(), VehicleControllers);

	for (AActor* VehicleController : VehicleControllers)
	{
		ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(VehicleController);
		auto* VehicleDescriptor = Response.add_vehicles();
		VehicleDescriptor->set_name(TCHAR_TO_UTF8(*Controller->GetVehicleName()));
		VehicleDescriptor->set_type("car");
	}

	TArray<AActor*> DroneControllers;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UTempoDroneControlInterface::StaticClass(), DroneControllers);

	for (AActor* DroneController : DroneControllers)
	{
		ITempoDroneControlInterface* Controller = Cast<ITempoDroneControlInterface>(DroneController);
		auto* VehicleDescriptor = Response.add_vehicles();
		VehicleDescriptor->set_name(TCHAR_TO_UTF8(*Controller->GetDroneName()));
		VehicleDescriptor->set_type("drone");
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}


void UTempoVehicleControlServiceSubsystem::HandleVehicleCommand(const VehicleCommandRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	TArray<AActor*> VehicleControllers;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UTempoVehicleControlInterface::StaticClass(), VehicleControllers);

	const FString RequestedVehicleName(UTF8_TO_TCHAR(Request.vehicle_name().c_str()));

	// If vehicle name is not specified and there is only one, assume the client wants that one
	if (RequestedVehicleName.IsEmpty())
	{
		if (VehicleControllers.IsEmpty())
		{
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "No commandable vehicles found"));
			return;
		}
		if (VehicleControllers.Num() > 1)
		{
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "More than one commandable vehicle found, vehicle name required."));
			return;
		}
		
		ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(VehicleControllers[0]);
		Controller->HandleDrivingInput(FNormalizedDrivingInput { Request.acceleration(), Request.steering() });
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
		return;
	}
	
	for (AActor* VehicleController : VehicleControllers)
	{
		ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(VehicleController);
		if (Controller->GetVehicleName().Equals(RequestedVehicleName, ESearchCase::IgnoreCase))
		{
			Controller->HandleDrivingInput(FNormalizedDrivingInput { Request.acceleration(), Request.steering() });
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
			return;
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a vehicle with the specified name"));
}

void UTempoVehicleControlServiceSubsystem::HandleDroneCommand(const DroneCommandRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	TArray<AActor*> DroneControllers;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UTempoDroneControlInterface::StaticClass(), DroneControllers);

	const FString RequestedVehicleName(UTF8_TO_TCHAR(Request.vehicle_name().c_str()));

	// If vehicle name is not specified and there is only one, assume the client wants that one
	if (RequestedVehicleName.IsEmpty())
	{
		if (DroneControllers.IsEmpty())
		{
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "No commandable drones found"));
			return;
		}
		if (DroneControllers.Num() > 1)
		{
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "More than one commandable drone found, vehicle name required."));
			return;
		}
		
		ITempoDroneControlInterface* Controller = Cast<ITempoDroneControlInterface>(DroneControllers[0]);
		Controller->HandleFlyingInput(FNormalizedFlyingInput { Request.pitch(), Request.roll(), Request.yaw(), Request.throttle() });
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
		return;
	}
	
	for (AActor* DroneController : DroneControllers)
	{
		ITempoDroneControlInterface* Controller = Cast<ITempoDroneControlInterface>(DroneController);
		if (Controller->GetDroneName().Equals(RequestedVehicleName, ESearchCase::IgnoreCase))
		{
			Controller->HandleFlyingInput(FNormalizedFlyingInput { Request.pitch(), Request.roll(), Request.yaw(), Request.throttle() });
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
			return;
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a drone with the specified name"));
}

