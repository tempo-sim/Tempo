// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoServer.h"

#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"
#include "TempoServerEndpoint.h"
#include "TempoServiceProvider.h"
#include "TempoCore.h"

#include "grpcpp/impl/service_type.h"
#if PLATFORM_WINDOWS
// gRPC transitively includes <windows.h>, which leaks wingdi.h's GetObject macro
// (GetObjectA/W). In TempoCore's unity TU, the next .cpp file's transitive include
// of WheeledVehiclePawn.h -> Chaos's ImplicitObjectScaled.h would otherwise see
// `Implicit.template GetObject<T>()` mangled to `GetObjectW` — not a member of
// FImplicitObject. Scrub the macro here so later parses are clean.
#undef GetObject
#endif

FTempoServer::FTempoServer()
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
	OnPostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FTempoServer::Initialize);
#else
	OnPostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FTempoServer::Initialize);
#endif
	OnPostWorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda(
	[this](UWorld* World, const UWorld::InitializationValues)
	{
		if (UTempoCoreUtils::IsGameWorld(World))
		{
			// The server has nothing to do with movie scene sequences, but this event fires in exactly the right conditions:
			// After world time has been updated for the current frame, before Actor ticks have begun, and even when paused.
			OnMovieSceneSequenceTickHandle = World->AddMovieSceneSequenceTickHandler(
				FOnMovieSceneSequenceTick::FDelegate::CreateLambda([this](float){
					TickInternal();
				}));
		}
	});
	OnPreWorldFinishDestroyHandle = FWorldDelegates::OnPreWorldFinishDestroy.AddLambda(
		[this](UWorld* World)
	{
		if (UTempoCoreUtils::IsGameWorld(World))
		{
			// Tick one more time to give deactivated services a chance to flush their final messages
			TickInternal();
			World->OnWorldBeginPlay.Remove(OnWorldBeginPlayHandle);
			World->RemoveMovieSceneSequenceTickHandler(OnMovieSceneSequenceTickHandle);
			OnMovieSceneSequenceTickHandle.Reset();
		}
	});

#if WITH_EDITOR
	GetMutableDefault<UTempoCoreSettings>()->OnSettingChanged().AddLambda([this](UObject* Object, struct FPropertyChangedEvent& Event)
	{
		if (Event.Property->GetName() == UTempoCoreSettings::GetServerTransportMemberName() ||
			Event.Property->GetName() == UTempoCoreSettings::GetServerPortMemberName() ||
			Event.Property->GetName() == UTempoCoreSettings::GetServerSocketPathMemberName() ||
			Event.Property->GetName() == UTempoCoreSettings::GetServerCompressionLevelMemberName())
		{
			Reinitialize();
		}
	});
#endif
}

FTempoServer::~FTempoServer()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitializationHandle);
	FWorldDelegates::OnPreWorldFinishDestroy.Remove(OnPreWorldFinishDestroyHandle);

	Deinitialize();
}

FTempoServer& FTempoServer::Get()
{
	FTempoCoreModule* TempoCoreModule = FModuleManager::GetModulePtr<FTempoCoreModule>(TEXT("TempoCore"));
	check(TempoCoreModule);
	return *TempoCoreModule->Server.Get();
}

grpc_compression_level CompressionLevelTogRPC(EServerCompressionLevel TempoCompressionLevel)
{
	switch (TempoCompressionLevel)
	{
	case EServerCompressionLevel::None:
		{
			return GRPC_COMPRESS_LEVEL_NONE;
		}
	case EServerCompressionLevel::Low:
		{
			return GRPC_COMPRESS_LEVEL_LOW;
		}
	case EServerCompressionLevel::Med:
		{
			return GRPC_COMPRESS_LEVEL_MED;
		}
	case EServerCompressionLevel::High:
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

void FTempoServer::Initialize()
{
	TArray<UObject*> ServiceProviderObjects;
	for (TObjectIterator<UObject> ObjectIt(RF_NoFlags); ObjectIt; ++ObjectIt)
	{
		UObject* Object = *ObjectIt;
		if (!IsValid(Object) || !Object->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		if (Object->Implements<UTempoServiceProvider>())
		{
			// Don't call RegisterServices yet - store it for later so we can only call RegisterServices
			// on the most derived instances (without an O(n^2) iteration over all UObjects)
			ServiceProviderObjects.Add(Object);
		}
	}

	for (UObject* Object : ServiceProviderObjects)
	{
		bool bMostDerived = true;
		for (const UObject* Other : ServiceProviderObjects)
		{
			if (Other->GetClass() != Object->GetClass() && Other->IsA(Object->GetClass()))
			{
				bMostDerived = false;
				break;
			}
		}
		if (bMostDerived)
		{
			Cast<ITempoServiceProvider>(Object)->RegisterServices(*this);
		}
	}
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	TempoServerEndpoint::FEndpoint Endpoint;
	FString EndpointError;
	if (!TempoServerEndpoint::ResolveEndpoint(Settings->GetServerTransport(), Settings->GetServerPort(),
		Settings->GetServerSocketPath(), Endpoint, EndpointError))
	{
		UE_LOG(LogTempoCore, Error, TEXT("Could not start Tempo gRPC server: %s"), *EndpointError);
		return;
	}

	const bool bUnixSocket = !Endpoint.SocketPath.IsEmpty();
	if (bUnixSocket)
	{
		// Unlike a taken TCP port, a socket file in the way does not fail the bind - gRPC removes
		// it first. Refuse up front instead, so a second server cannot take the name out from
		// under a running one. See TempoServerEndpoint::IsSocketPathInUse.
		if (TempoServerEndpoint::IsSocketPathInUse(Endpoint.SocketPath))
		{
			UE_LOG(LogTempoCore, Error, TEXT("Error while starting Tempo gRPC server. Another server is already listening on %s."), *Endpoint.SocketPath);
			return;
		}
		if (!TempoServerEndpoint::EnsureSocketDirectory(Endpoint.SocketPath, EndpointError))
		{
			UE_LOG(LogTempoCore, Error, TEXT("Could not start Tempo gRPC server: %s"), *EndpointError);
			return;
		}
	}

	grpc::ServerBuilder Builder;
	Builder.AddListeningPort(TCHAR_TO_UTF8(*Endpoint.Target), grpc::InsecureServerCredentials());
	if (!bUnixSocket)
	{
		// gRPC enables SO_REUSEPORT by default where supported, which would let a second instance silently share
		// the port (with the kernel load-balancing connections between them). Disable it so the bind fails instead.
		// SO_REUSEPORT has no meaning for a Unix domain socket, which the probe above covers instead.
		Builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
	}
	for (const auto& Service : Services)
	{
		Builder.RegisterService(Service.Value.Get());
	}

	Builder.SetDefaultCompressionLevel(CompressionLevelTogRPC(Settings->GetServerCompressionLevel()));

	CompletionQueue.Reset(Builder.AddCompletionQueue().release());
	Server.Reset(Builder.BuildAndStart().release());

	if (!Server.Get())
	{
		UE_LOG(LogTempoCore, Error, TEXT("Error while starting Tempo gRPC server. Perhaps %s was not available."), *Endpoint.Target);
		return;
	}

	// Held so Deinitialize can remove the socket file, which gRPC leaves behind.
	BoundSocketPath = Endpoint.SocketPath;

	UE_LOG(LogTempoCore, Display, TEXT("Tempo gRPC server listening on %s"), *Endpoint.Target);

	// Now that the server has started we can initialize the request managers.
	for (const auto& RequestManager : RequestManagers)
	{
		RequestManager.Value->Init(CompletionQueue.Get());
	}

	bIsInitialized = true;
}

void FTempoServer::Deinitialize()
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

	Services.Empty();
	RequestManagers.Empty();

	if (!BoundSocketPath.IsEmpty())
	{
		TempoServerEndpoint::RemoveSocketFile(BoundSocketPath);
		BoundSocketPath.Reset();
	}
}

void FTempoServer::Reinitialize()
{
	TMap<FName, FWeakObjectPtr> PreviouslyActiveServices;
	for (const auto& RequestManager : RequestManagers)
	{
		if (RequestManager.Value->GetActiveObject().IsValid())
		{
			PreviouslyActiveServices.Add(RequestManager.Value->GetServiceName(), RequestManager.Value->GetActiveObject());
		}
	}
	Deinitialize();
	Initialize();
	for (const auto& PreviouslyActiveService : PreviouslyActiveServices)
	{
		if (PreviouslyActiveService.Value.IsValid())
		{
			UE_LOG(LogTempoCore, Display, TEXT("Reactivating service %s"), *PreviouslyActiveService.Key.ToString());
			ActivateService(PreviouslyActiveService.Key, PreviouslyActiveService.Value.Get());
		}
	}
}

void FTempoServer::Tick(float DeltaTime)
{
	// In game we tick via the world's MovieSceneSequenceTick, which happens near the beginning of the frame,
	// as opposed to TickableObject ticks, which tick near the end of the frame, to flush messages published
	// at the very end of the last frame as soon as possible.
	if (!OnMovieSceneSequenceTickHandle.IsValid())
	{
		TickInternal();
	}
}

void FTempoServer::TickInternal()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoServerTick);

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
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoServerLoop);

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
				// In FixedStep mode, keep draining until no manager has an in-flight write/finish
				// or queued responses. We re-evaluate every iteration so managers that transitioned
				// into RESPONDING mid-tick (or that still have queued responses behind an in-flight
				// write) are waited on too.
				if (TimeMode == ETimeMode::FixedStep)
				{
					bool bAnyUnflushed = false;
					for (const auto& Elem : RequestManagers)
					{
						if (Elem.Value->HasUnflushedWork())
						{
							bAnyUnflushed = true;
							break;
						}
					}
					bProcessedPendingEvents = !bAnyUnflushed;
				}
				else
				{
					bProcessedPendingEvents = true;
				}
				break;
			}
		}
	}
}

void FTempoServer::HandleEventForTag(int32 Tag, bool bOk)
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

TStatId FTempoServer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FTempoServer, STATGROUP_Tickables);
}
