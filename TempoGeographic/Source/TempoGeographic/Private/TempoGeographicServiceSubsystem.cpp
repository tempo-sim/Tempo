// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGeographicServiceSubsystem.h"

#include "TempoDateTimeSystem.h"
#include "TempoGeographic.h"
#include "TempoGeoReferencingSystem.h"

#include "GeoReferencingSystem.h"

#include "Kismet/GameplayStatics.h"

#include "TempoGeographic/Geographic.grpc.pb.h"

using GeographicService = TempoGeographic::GeographicService::AsyncService;

ATempoDateTimeSystem* GetTempoDateTimeSystem(const UObject* WorldContextObject)
{
	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsOfClass(WorldContextObject, ATempoDateTimeSystem::StaticClass(), Actors);
	if (Actors.Num() > 1)
	{
		UE_LOG(LogTempoGeographic, Error, TEXT("More than one TempoDateTime actor found in World"));
	}
	if (Actors.Num() == 0)
	{
		return nullptr;
	}
	return Cast<ATempoDateTimeSystem>(Actors[0]);
}

void UTempoGeographicServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer* ScriptingServer)
{
	ScriptingServer->RegisterService<GeographicService>(
		TSimpleRequestHandler<GeographicService, TempoGeographic::Date, TempoScripting::Empty>(&GeographicService::RequestSetDate).BindUObject(this, &UTempoGeographicServiceSubsystem::SetDate),
		TSimpleRequestHandler<GeographicService, TempoGeographic::TimeOfDay, TempoScripting::Empty>(&GeographicService::RequestSetTimeOfDay).BindUObject(this, &UTempoGeographicServiceSubsystem::SetTimeOfDay),
		TSimpleRequestHandler<GeographicService, TempoGeographic::DayCycleRateRequest, TempoScripting::Empty>(&GeographicService::RequestSetDayCycleRelativeRate).BindUObject(this, &UTempoGeographicServiceSubsystem::SetDayCycleRelativeRate),
		TSimpleRequestHandler<GeographicService, TempoScripting::Empty, TempoGeographic::DateTime>(&GeographicService::RequestGetDateTime).BindUObject(this, &UTempoGeographicServiceSubsystem::GetDateTime),
		TSimpleRequestHandler<GeographicService, TempoGeographic::GeographicCoordinate, TempoScripting::Empty>(&GeographicService::RequestSetGeographicReference).BindUObject(this, &UTempoGeographicServiceSubsystem::SetGeographicReference)
		);
}

void UTempoGeographicServiceSubsystem::SetDate(const TempoGeographic::Date& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (ATempoDateTimeSystem* DateTimeSystem = GetTempoDateTimeSystem(this))
	{
		const FDateTime CurrentDateTime = DateTimeSystem->GetSimDateTime();
		const FDateTime RequestedDateTime(Request.year(), Request.month(), Request.day(), CurrentDateTime.GetHour(), CurrentDateTime.GetMinute(), CurrentDateTime.GetSecond(), CurrentDateTime.GetMillisecond());
		DateTimeSystem->AdvanceSimDateTime(RequestedDateTime - CurrentDateTime);
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
		return;
	}
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "No TempoDateTime actor found"));
}

void UTempoGeographicServiceSubsystem::SetTimeOfDay(const TempoGeographic::TimeOfDay& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (ATempoDateTimeSystem* DateTimeSystem = GetTempoDateTimeSystem(this))
	{
		const FDateTime CurrentDateTime = DateTimeSystem->GetSimDateTime();
		const FDateTime RequestedDateTime(CurrentDateTime.GetYear(), CurrentDateTime.GetMonth(), CurrentDateTime.GetDay(), Request.hour(), Request.minute(), Request.second(), 0);
		DateTimeSystem->AdvanceSimDateTime(RequestedDateTime - CurrentDateTime);
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
		return;
	}
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "No TempoDateTime actor found"));
}

void UTempoGeographicServiceSubsystem::SetDayCycleRelativeRate(const TempoGeographic::DayCycleRateRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (ATempoDateTimeSystem* DateTimeSystem = GetTempoDateTimeSystem(this))
	{
		DateTimeSystem->SetDayCycleRelativeRate(Request.rate());
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
		return;
	}
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "No TempoDateTime actor found"));
}

void UTempoGeographicServiceSubsystem::GetDateTime(const TempoScripting::Empty& Request, const TResponseDelegate<TempoGeographic::DateTime>& ResponseContinuation)
{
	TempoGeographic::DateTime Response;
	if (ATempoDateTimeSystem* DateTimeSystem = GetTempoDateTimeSystem(this))
	{
		const FDateTime CurrentDateTime = DateTimeSystem->GetSimDateTime();
		Response.mutable_date()->set_year(CurrentDateTime.GetYear());
		Response.mutable_date()->set_month(CurrentDateTime.GetMonth());
		Response.mutable_date()->set_day(CurrentDateTime.GetDay());
		Response.mutable_time()->set_hour(CurrentDateTime.GetHour());
		Response.mutable_time()->set_minute(CurrentDateTime.GetMinute());
		Response.mutable_time()->set_second(CurrentDateTime.GetSecond());
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
		return;
	}
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "No TempoDateTime actor found"));
}

void UTempoGeographicServiceSubsystem::SetGeographicReference(const TempoGeographic::GeographicCoordinate& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (ATempoGeoReferencingSystem* GeoReferencingSystem = Cast<ATempoGeoReferencingSystem>(AGeoReferencingSystem::GetGeoReferencingSystem(this)))
	{
		GeoReferencingSystem->OriginLatitude = Request.latitude();
		GeoReferencingSystem->OriginLongitude = Request.longitude();
		GeoReferencingSystem->OriginAltitude = Request.altitude();
		// Ideally we would have overriden ApplySettings to do the broadcast but it is not virtual.
		GeoReferencingSystem->ApplySettings();
		GeoReferencingSystem->BroadcastGeographicReferenceChanged();
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
		return;
	}
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "GeoReferencingSystem not found"));
}
