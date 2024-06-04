// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoScriptingServer.h"

#include "TempoCoreSettings.h"
#include "TempoScripting.h"

#include "grpcpp/impl/service_type.h"

UTempoScriptingServer::UTempoScriptingServer() = default;
UTempoScriptingServer::UTempoScriptingServer(FVTableHelper& Helper)
	: Super(Helper) {}
UTempoScriptingServer::~UTempoScriptingServer() = default;

grpc_compression_level CompressionLevelTogRPC(EScriptingCompressionLevel TempoLevel)
{
	switch (TempoLevel)
	{
	case EScriptingCompressionLevel::None:
		{
			return GRPC_COMPRESS_LEVEL_NONE;
		}
	case EScriptingCompressionLevel::Low:
		{
			return GRPC_COMPRESS_LEVEL_LOW;
		}
	case EScriptingCompressionLevel::Med:
		{
			return GRPC_COMPRESS_LEVEL_MED;
		}
	case EScriptingCompressionLevel::High:
		{
			return GRPC_COMPRESS_LEVEL_HIGH;
		}
	default:
		{
			checkf(false, TEXT("Unhandled compression level"));
			return GRPC_COMPRESS_LEVEL_COUNT;
		}
	}
}

void UTempoScriptingServer::Initialize(int32 Port)
{
	const FString ServerAddress = FString::Printf(TEXT("0.0.0.0:%d"), Port);
	grpc::ServerBuilder Builder;
	Builder.AddListeningPort(TCHAR_TO_UTF8(*ServerAddress), grpc::InsecureServerCredentials());
	for (const TUniquePtr<grpc::Service>& Service : Services)
	{
		Builder.RegisterService(Service.Get());
	}

	Builder.SetDefaultCompressionLevel(CompressionLevelTogRPC(GetDefault<UTempoCoreSettings>()->GetScriptingCompressionLevel()));

	CompletionQueue.Reset(Builder.AddCompletionQueue().release());
	Server.Reset(Builder.BuildAndStart().release());

	if (!Server.Get())
	{
		UE_LOG(LogTempoScripting, Error, TEXT("Error while starting scripting server: %s. Perhaps port %d was not available."), *GetName(), Port);
		return;
	}
	
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

	checkf(Server.Get(), TEXT("Server was unexpectedly null"));
	checkf(CompletionQueue.Get(), TEXT("CompletionQueue was unexpectedly null"));

	static constexpr int32 MaxShutdownTimeNanoSeconds = 5e7; // 0.05s
	static constexpr gpr_timespec MaxShutdownWaitTime {0, MaxShutdownTimeNanoSeconds, GPR_TIMESPAN};
	Server->Shutdown(MaxShutdownWaitTime);
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

	TSet<int32> ManagersWithPendingWrites;
	for (const auto& Elem : RequestManagers)
	{
		const int32 Tag = Elem.Key;
		const TSharedPtr<FRequestManager>& RequestManager = Elem.Value;
		const FRequestManager::EState State = RequestManager->GetState();
		if (State == FRequestManager::EState::RESPONDING ||
			State == FRequestManager::EState::FINISHING)
		{
			ManagersWithPendingWrites.Add(Tag);
		}
	}
	
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
				ManagersWithPendingWrites.Remove(*Tag);
				HandleEventForTag(*Tag, bOk);
				break;
			}
		case grpc::CompletionQueue::SHUTDOWN:
			{
				checkf(false, TEXT("gRPC Completion Queue got unexpected shutdown event."));
				// Intentional fallthrough.
			}
		case grpc::CompletionQueue::TIMEOUT:
			{
				// If we've processed all the pending events (which, in fixed time mode, includes an event for every manager with a pending write), move on.
				bProcessedPendingEvents = TimeMode == ETimeMode::FixedStep ? ManagersWithPendingWrites.IsEmpty() : true;
				break;
			}
		}
	}
}

void UTempoScriptingServer::HandleEventForTag(int32 Tag, bool bOk)
{
	if (TSharedPtr<FRequestManager>* RequestManager = RequestManagers.Find(Tag))
	{
		if (!bOk)
		{
			RequestManagers.Remove(Tag);
			return;
		}
		
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
		case FRequestManager::HANDLING: // Shouldn't happen.
			{
				checkf(false, TEXT("Got event for request manager while it was handling another request."))
				break;
			}
		case FRequestManager::RESPONDING: // A response has been sent, and there are more to come.
			{
				(*RequestManager)->HandleAndRespond();
				break;
			}
		case FRequestManager::FINISHING: // The rpc has finished.
			{
				RequestManagers.Remove(Tag);
				break;
			}
		}
	}
}
