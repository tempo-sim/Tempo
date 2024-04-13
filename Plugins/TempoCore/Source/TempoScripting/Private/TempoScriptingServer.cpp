// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoScriptingServer.h"

#include "TempoCoreSettings.h"
#include "TempoScripting.h"

#include "grpcpp/impl/service_type.h"

UTempoScriptingServer::UTempoScriptingServer() = default;
UTempoScriptingServer::UTempoScriptingServer(FVTableHelper& Helper) {}
UTempoScriptingServer::~UTempoScriptingServer() = default;

void UTempoScriptingServer::Initialize(int32 Port)
{
	const FString ServerAddress = FString::Printf(TEXT("0.0.0.0:%d"), Port);
	grpc::ServerBuilder Builder;
	Builder.AddListeningPort(TCHAR_TO_UTF8(*ServerAddress), grpc::InsecureServerCredentials());
	for (const TUniquePtr<grpc::Service>& Service : Services)
	{
		Builder.RegisterService(Service.Get());
	}

	CompletionQueue.Reset(Builder.AddCompletionQueue().release());
	Server.Reset(Builder.BuildAndStart().release());
	UE_LOG(LogTempoScripting, Display, TEXT("Tempo gRPC server listening on %s"), *ServerAddress);

	// Now that the server has started we can initialize the request managers.
	for (const auto& RequestManager : RequestManagers)
	{
		RequestManager.Value->Init(CompletionQueue.Get());
	}
	
	bIsInitialized = true;
}

void UTempoScriptingServer::Deinitialize()
{
	if (!bIsInitialized)
	{
		return;
	}
	
	bIsInitialized = false;
	
	Server->Shutdown();
	CompletionQueue->Shutdown();

	// Flush (and discard) all pending events (until we get the shutdown event).
	int32* Tag;
	bool bOk;
	while (CompletionQueue->Next(reinterpret_cast<void**>(&Tag), &bOk))
	{
		if (!bOk)
		{
			// gRPC gives us one event per tag with bOk=false to clean up.
			RequestManagers.Remove(*Tag);
		}
	}
}

void UTempoScriptingServer::Tick(float DeltaTime)
{
	if (!bIsInitialized)
	{
		return;
	}
	
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	const ETimeMode TimeMode = Settings->GetTimeMode();
	const int32 MaxEventProcessingTimeMicroSeconds = Settings->GetMaxEventProcessingTime();
	const double MaxEventProcessingTimeSeconds = MaxEventProcessingTimeMicroSeconds / 1.e6;
	const int32 MaxEventWaitTimeNanoSeconds = Settings->GetMaxEventWaitTime();

	bool bProcessedPendingEvents = false;
	const double Start = FPlatformTime::Seconds();
	while (!bProcessedPendingEvents)
	{
		// In FixedStep mode process all pending requests before proceeding. Otherwise limit time spent processing.
		if (TimeMode != ETimeMode::FixedStep && FPlatformTime::Seconds() - Start > MaxEventProcessingTimeSeconds)
		{
			break;
		}
		
		int32* Tag;
		bool bOk;
		const gpr_timespec MaxEventWaitTime {0, MaxEventWaitTimeNanoSeconds, GPR_TIMESPAN};
		switch (grpc::CompletionQueue::NextStatus Status = CompletionQueue->AsyncNext(reinterpret_cast<void**>(&Tag), &bOk, MaxEventWaitTime))
		{
		case grpc::CompletionQueue::GOT_EVENT:
			{
				// Handle the event and then wait for another.
				GPR_ASSERT(bOk);
				HandleEventForTag(*Tag);
				break;
			}
		case grpc::CompletionQueue::SHUTDOWN:
			{
				checkf(false, TEXT("gRPC Completion Queue got unexpected shutdown event."));
				// Intentional fallthrough.
			}
		case grpc::CompletionQueue::TIMEOUT:
			{
				// We've processed all the pending events, move on.
				bProcessedPendingEvents = true;
				break;
			}
		}
	}
}

void UTempoScriptingServer::HandleEventForTag(int32 Tag)
{
	if (TUniquePtr<FRequestManager>* RequestManager = RequestManagers.Find(Tag))
	{
		switch ((*RequestManager)->GetState())
		{
		case FRequestManager::UNINITIALIZED: // Shouldn't happen.
			{
				checkf(false, TEXT("Got event for uninitialized request manager."))
				break;
			}
		case FRequestManager::REQUESTED: // A request has been received.
			{
				// Immediately prepare to receive another request.
				const int32 NewTag = TagAllocator++;
				RequestManagers.Emplace(NewTag, (*RequestManager)->Duplicate(NewTag))->Init(CompletionQueue.Get());
	
				(*RequestManager)->HandleAndRespond();
				break;
			}
		case FRequestManager::RESPONDED: // The response has been sent.
			{
				RequestManagers.Remove(Tag);
				break;
			}
		}
	}
}
