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

void UTempoDrivingServiceSubsystem::RegisterWorldServices(UTempoScriptingServer* ScriptingServer)
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
		Response.add_vehicle_id(Controller->GetVehicleId());
	}
	
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}


void UTempoDrivingServiceSubsystem::HandleDrivingCommand(const DrivingCommandRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	TArray<AActor*> VehicleControllers;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UTempoVehicleControlInterface::StaticClass(), VehicleControllers);

	for (AActor* VehicleController : VehicleControllers)
	{
		ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(VehicleController);
		if (Controller->GetVehicleId() == Request.vehicle_id())
		{
			Controller->HandleDrivingInput(FNormalizedDrivingInput { Request.acceleration(), Request.steering() });
			
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
			return;
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a vehicle with the given Id"));
}
