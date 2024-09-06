// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDrivingServiceSubsystem.h"

#include "TempoVehicleControlInterface.h"
#include "TempoVehicles/Driving.grpc.pb.h"
#include "TempoScripting/Empty.pb.h"

#include "Kismet/GameplayStatics.h"

using DrivingService = TempoVehicles::DrivingService::AsyncService;
using DrivingCommandRequest = TempoVehicles::DrivingCommandRequest;
using CommandableVehiclesResponse = TempoVehicles::CommandableVehiclesResponse;
using TempoEmpty = TempoScripting::Empty;

void UTempoDrivingServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer* ScriptingServer)
{
	ScriptingServer->RegisterService<DrivingService>(
		TSimpleRequestHandler<DrivingService, DrivingCommandRequest, TempoEmpty>(&DrivingService::RequestCommandVehicle).BindUObject(this, &UTempoDrivingServiceSubsystem::HandleDrivingCommand),
		TSimpleRequestHandler<DrivingService, TempoEmpty, CommandableVehiclesResponse>(&DrivingService::RequestGetCommandableVehicles).BindUObject(this, &UTempoDrivingServiceSubsystem::GetCommandableVehicles)
		);
}

void UTempoDrivingServiceSubsystem::GetCommandableVehicles(const TempoScripting::Empty& Request, const TResponseDelegate<TempoVehicles::CommandableVehiclesResponse>& ResponseContinuation) const
{
	TArray<AActor*> VehicleControllers;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UTempoVehicleControlInterface::StaticClass(), VehicleControllers);

	CommandableVehiclesResponse Response;
	for (AActor* VehicleController : VehicleControllers)
	{
		ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(VehicleController);
		Response.add_vehicle_name(TCHAR_TO_UTF8(*Controller->GetVehicleName()));
	}
	
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}


void UTempoDrivingServiceSubsystem::HandleDrivingCommand(const DrivingCommandRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
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
