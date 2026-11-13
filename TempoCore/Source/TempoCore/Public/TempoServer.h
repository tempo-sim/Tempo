// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoServiceProvider.h"

// gRPC's port_platform.h includes <windows.h> directly. Route it through Unreal's MinWindows.h
// first so the Windows headers are trimmed and their macros are scrubbed afterwards. gRPC's
// atm_windows.h also uses the Win32 Interlocked* and MemoryBarrier macros, which Unreal hides.
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#ifndef MemoryBarrier
#define MemoryBarrier() FPlatformMisc::MemoryBarrier()
#define TEMPO_DEFINED_MEMORY_BARRIER
#endif
#endif
#include <grpcpp/grpcpp.h>
#if PLATFORM_WINDOWS
#ifdef TEMPO_DEFINED_MEMORY_BARRIER
#undef MemoryBarrier
#undef TEMPO_DEFINED_MEMORY_BARRIER
#endif
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

template <class ResponseType>
using TResponseDelegate = TDelegate<void(const ResponseType&, grpc::Status)>;

namespace grpc
{
	static const Status Status_OK;
}

template <class AsyncServiceType, class RequestType, class ResponseType, template <class> class ResponderType, class UserObjectType, bool Const>
struct TRequestHandler
{
	typedef AsyncServiceType HandlerServiceType;
	typedef RequestType HandlerRequestType;
	typedef ResponseType HandlerResponseType;
	typedef ResponderType<ResponseType> HandlerResponderType;
	typedef UserObjectType HandlerUserObjectType;

	typedef typename TMemFunPtrType<false, AsyncServiceType, void(grpc::ServerContext*, RequestType*, ResponderType<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)>::Type AcceptFuncType;
	typedef typename TMemFunPtrType<Const, UserObjectType, void(const RequestType&, const TResponseDelegate<ResponseType>&)>::Type HandleFuncType;

	TRequestHandler(AcceptFuncType AcceptFuncIn, HandleFuncType HandleFuncIn)
		: AcceptFunc(AcceptFuncIn), HandleFunc(HandleFuncIn)
	{
		static_assert(std::is_base_of_v<grpc::Service, AsyncServiceType>);
	}

	void Init(AsyncServiceType* Service)
	{
		AcceptDelegate.BindRaw(Service, AcceptFunc);
	}

	// Marks an RPC that may only run in a game frame, inside a world tick: one that changes the step
	// grid, loads or leaves a level, or changes engine state. While a game frame is part-way through
	// its steps, its requests are held; the game frame then runs the rest of its steps, and they are
	// handled in its frame.
	TRequestHandler GameFrameOnly() const
	{
		TRequestHandler Handler(*this);
		Handler.bGameFrameOnly = true;
		return Handler;
	}

	bool IsGameFrameOnly() const { return bGameFrameOnly; }

	void AcceptRequests(grpc::ServerContext* Context, RequestType* Request, ResponderType<ResponseType>* Responder, grpc::ServerCompletionQueue* CompletionQueue, void* Tag)
	{
		check(AcceptDelegate.IsBound());
		AcceptDelegate.Execute(Context, Request, Responder, CompletionQueue, CompletionQueue, Tag);
	}

	void HandleRequest(const RequestType& Request, const TResponseDelegate<ResponseType>& ResponseDelegate)
	{
		if (!HandleDelegate.ExecuteIfBound(Request, ResponseDelegate))
		{
			ResponseDelegate.ExecuteIfBound(ResponseType(), grpc::Status(grpc::UNAVAILABLE, "Service is not active"));
		}
	}

	void Activate(UserObjectType* UserObject)
	{
		if (!ensureAlwaysMsgf(!UserObject->HasAnyFlags(RF_ClassDefaultObject), TEXT("Activate called on CDO! Do not call ActivateService from RegisterServices.")))
		{
			return;
		}
		ensureAlwaysMsgf(!HandleDelegate.IsBound() || HandleDelegate.IsBoundToObject(UserObject), TEXT("Handle delegate was already bound to another object."));
		HandleDelegate.BindUObject(UserObject, HandleFunc);
		ActiveObject = UserObject;
	}

	void Deactivate()
	{
		HandleDelegate.Unbind();
		ActiveObject.Reset();
	}

	const FWeakObjectPtr& GetActiveObject() const
	{
		return ActiveObject;
	}

private:
	AcceptFuncType AcceptFunc;
	HandleFuncType HandleFunc;
	TDelegate<void(const RequestType&, const TResponseDelegate<ResponseType>&)> HandleDelegate;
	TDelegate<void(grpc::ServerContext*, RequestType*, ResponderType<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)> AcceptDelegate;
	FWeakObjectPtr ActiveObject;
	bool bGameFrameOnly = false;
};

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType, bool Const>
using TSimpleRequestHandler = TRequestHandler<ServiceType, RequestType, ResponseType, grpc::ServerAsyncResponseWriter, UserObjectType, Const>;

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType, bool Const>
using TStreamingRequestHandler = TRequestHandler<ServiceType, RequestType, ResponseType, grpc::ServerAsyncWriter, UserObjectType, Const>;

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType>
TSimpleRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, false>
SimpleRequestHandler(void(ServiceType::*AcceptFunc)(grpc::ServerContext*, RequestType*, grpc::ServerAsyncResponseWriter<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*),
	void(UserObjectType::*HandleFunc)(const RequestType&, const TResponseDelegate<ResponseType>&))
{
	return TSimpleRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, false>(AcceptFunc, HandleFunc);
}

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType>
TSimpleRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, true>
SimpleRequestHandler(void(ServiceType::*AcceptFunc)(grpc::ServerContext*, RequestType*, grpc::ServerAsyncResponseWriter<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*),
	void(UserObjectType::*HandleFunc)(const RequestType&, const TResponseDelegate<ResponseType>&) const)
{
	return TSimpleRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, true>(AcceptFunc, HandleFunc);
}

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType>
TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, false>
StreamingRequestHandler(void(ServiceType::*AcceptFunc)(grpc::ServerContext*, RequestType*, grpc::ServerAsyncWriter<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*),
	void(UserObjectType::*HandleFunc)(const RequestType&, const TResponseDelegate<ResponseType>&))
{
	return TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, false>(AcceptFunc, HandleFunc);
}

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType>
TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, true>
StreamingRequestHandler(void(ServiceType::*AcceptFunc)(grpc::ServerContext*, RequestType*, grpc::ServerAsyncWriter<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*),
	void(UserObjectType::*HandleFunc)(const RequestType&, const TResponseDelegate<ResponseType>&) const)
{
	return TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, true>(AcceptFunc, HandleFunc);
}

/**
 * A request manager manages the lifecycle of one gRPC request.
 * This interface allows ownership by the FTempoServer without visibility into the concrete handler types.
 */
struct FRequestManager : TSharedFromThis<FRequestManager>
{
	virtual ~FRequestManager() = default;
	enum EState { UNINITIALIZED, REQUESTED, HANDLING, RESPONDING, FINISHING };
	virtual EState GetState() const = 0;
	virtual bool HasUnflushedWork() const = 0;
	virtual void Init(grpc::ServerCompletionQueue* CompletionQueue) = 0;
	virtual void HandleAndRespond() = 0;
	virtual FRequestManager* Duplicate(int32 NewTag) const = 0;
	virtual const FName GetServiceName() const = 0;
	virtual bool IsGameFrameOnly() const = 0;
	virtual void Activate(UObject* Object) = 0;
	virtual void Deactivate() = 0;
	virtual const FWeakObjectPtr& GetActiveObject() const = 0;
};

template <class HandlerType>
class TRequestManagerBase: public FRequestManager {
public:
	TRequestManagerBase(int32 TagIn, const FName& ServiceNameIn, const typename HandlerType::HandlerServiceType* ServiceIn, TSharedPtr<HandlerType> HandlerIn)
		: State(UNINITIALIZED), Tag(TagIn), Handler(HandlerIn), ServiceName(ServiceNameIn), Service(ServiceIn), Responder(&Context) {}

	virtual EState GetState() const override { return State; }

	virtual bool HasUnflushedWork() const override
	{
		// A write or finish is in flight — we must wait for the completion event.
		return State == FRequestManager::EState::RESPONDING || State == FRequestManager::EState::FINISHING;
	}

	virtual void Init(grpc::ServerCompletionQueue* CompletionQueue) override
	{
		check(State == UNINITIALIZED);
		Handler->AcceptRequests(&Context, &Request, &Responder, CompletionQueue, &Tag);
		State = REQUESTED;
	}

	virtual const FName GetServiceName() const override
	{
		return ServiceName;
	}

	virtual bool IsGameFrameOnly() const override
	{
		return Handler->IsGameFrameOnly();
	}

	virtual void Activate(UObject* Object) override
	{
		Handler->Activate(CastChecked<typename HandlerType::HandlerUserObjectType>(Object));
	}

	virtual void Deactivate() override
	{
		Handler->Deactivate();
		if (State == HANDLING)
		{
			Handler->HandleRequest(Request, ResponseDelegate);
		}
	}

	virtual const FWeakObjectPtr& GetActiveObject() const override
	{
		return Handler->GetActiveObject();
	}

protected:
	EState State;
	int32 Tag;
	const TSharedPtr<HandlerType> Handler;
	const FName ServiceName;
	const typename HandlerType::HandlerServiceType* Service;
	typename HandlerType::HandlerRequestType Request;
	grpc::ServerContext Context;
	typename HandlerType::HandlerResponderType Responder;
	TResponseDelegate<typename HandlerType::HandlerResponseType> ResponseDelegate;
};

template <class HandlerType>
class TRequestManager : public TRequestManagerBase<HandlerType>
{
	using TRequestManagerBase<HandlerType>::TRequestManagerBase;
};

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType, bool Const>
class TRequestManager<TSimpleRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>> :
	public TRequestManagerBase<TSimpleRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>
{
	using TRequestManagerBase<TSimpleRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>::TRequestManagerBase;
	using Base = TRequestManagerBase<TSimpleRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>;

public:
	virtual void HandleAndRespond() override
	{
		check(Base::State == FRequestManager::EState::REQUESTED);
		Base::ResponseDelegate = TResponseDelegate<ResponseType>::CreateSPLambda(static_cast<FRequestManager*>(this), [this](const ResponseType& Response, grpc::Status Result)
		{
			Base::State = FRequestManager::EState::FINISHING;
			Base::Responder.Finish(Response, Result, &(Base::Tag));
		});
		Base::State = FRequestManager::EState::HANDLING;
		Base::Handler->HandleRequest(Base::Request, Base::ResponseDelegate);
	}

	virtual FRequestManager* Duplicate(int32 NewTag) const override
	{
		return new TRequestManager(NewTag, Base::ServiceName, Base::Service, Base::Handler);
	}
};

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType, bool Const>
class TRequestManager<TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>> :
	public TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>
{
	using TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>::TRequestManagerBase;
	using Base = TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>;

public:
	virtual bool HasUnflushedWork() const override
	{
		FScopeLock Lock(&ResponseMutex);
		// Either a write/finish is in flight, or we have responses queued behind the in-flight write.
		return Base::State == FRequestManager::EState::RESPONDING
			|| Base::State == FRequestManager::EState::FINISHING
			|| !ResponseQueue.IsEmpty();
	}

	virtual void HandleAndRespond() override
	{
		check(Base::State == FRequestManager::EState::REQUESTED || Base::State == FRequestManager::EState::RESPONDING);

		if (!Base::ResponseDelegate.IsBound())
		{
			// First invocation for this request. Bind the delegate and run the user handler.
			// The delegate writes immediately if no write is in flight; otherwise it enqueues
			// for the next write-completion event to drain.
			// A sensor's decode task runs this from a worker thread while the game thread handles this
			// stream's write completions below (see UTempoSensorServiceSubsystem::OnBetweenFramesPoll).
			Base::ResponseDelegate = TResponseDelegate<ResponseType>::CreateSPLambda(static_cast<FRequestManager*>(this), [this](const ResponseType& Response, grpc::Status Result)
			{
				FScopeLock Lock(&ResponseMutex);
				if (Base::State == FRequestManager::EState::HANDLING)
				{
					Respond(Response, Result);
				}
				else
				{
					ResponseQueue.Enqueue(TPair<ResponseType, grpc::Status>(Response, Result));
				}
			});
			Base::State = FRequestManager::EState::HANDLING;
			Base::Handler->HandleRequest(Base::Request, Base::ResponseDelegate);
			return;
		}

		// A write just completed. If more responses are queued, write the next one.
		// Otherwise ask the user handler for more.
		bool bResponded;
		{
			FScopeLock Lock(&ResponseMutex);
			TPair<ResponseType, grpc::Status> ResponseItem;
			bResponded = ResponseQueue.Dequeue(ResponseItem);
			if (bResponded)
			{
				Respond(ResponseItem.Key, ResponseItem.Value);
			}
			else
			{
				Base::State = FRequestManager::EState::HANDLING;
			}
		}
		if (!bResponded)
		{
			Base::Handler->HandleRequest(Base::Request, Base::ResponseDelegate);
		}
	}

	virtual FRequestManager* Duplicate(int32 NewTag) const override
	{
		return new TRequestManager(NewTag, Base::ServiceName, Base::Service, Base::Handler);
	}

	void Respond(const ResponseType& Response, grpc::Status Result)
	{
		if (!Result.ok())
		{
			// Consider non-OK result to mean there are no more responses available.
			Base::State = FRequestManager::EState::FINISHING;
			Base::Responder.Finish(Result, &(Base::Tag));
			return;
		}
		Base::State = FRequestManager::EState::RESPONDING;
		Base::Responder.Write(Response, &(Base::Tag));
	}

	TQueue<TPair<ResponseType, grpc::Status>> ResponseQueue;
	mutable FCriticalSection ResponseMutex;
};

/**
 * Hosts a gRPC server and supports registering arbitrary gRPC services and handlers.
 */
class TEMPOCORE_API FTempoServer : public FTickableGameObject
{
public:
	FTempoServer();
	virtual ~FTempoServer() override;

	static FTempoServer& Get();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	template <class ServiceType, class... HandlerTypes>
	void RegisterService(HandlerTypes... Handlers)
	{
		using AsyncServiceType = typename ServiceType::AsyncService;
		const FName ServiceName(UTF8_TO_TCHAR(ServiceType::service_full_name()));
		if (Services.Contains(ServiceName))
		{
			// Already registered - perhaps by calling Activate before Register
			return;
		}
		AsyncServiceType* Service = new AsyncServiceType();
		Services.Emplace(ServiceName, Service);
		RegisterHandlers<AsyncServiceType, HandlerTypes...>(ServiceName, Service, Handlers...);
	}

	template <class ServiceType>
	void ActivateService(UObject* Object)
	{
		const FName ServiceName(UTF8_TO_TCHAR(ServiceType::service_full_name()));
		ActivateService(ServiceName, Object);
	}

	template <class ServiceType>
	void DeactivateService()
	{
		const FName ServiceName(UTF8_TO_TCHAR(ServiceType::service_full_name()));
		DeactivateService(ServiceName);
	}

	// Handles gRPC events on the game thread until ShouldStop returns true, or for at most
	// TimeoutSeconds. OnPoll runs every PollIntervalSeconds, whether the queue is idle or busy.
	// Returns whether ShouldStop ended it. This is how ATempoWorldSettings serves RPCs between the
	// steps a client advances between frames, outside TickInternal.
	bool PumpEvents(TFunctionRef<bool()> ShouldStop, double TimeoutSeconds, double PollIntervalSeconds, TFunctionRef<void()> OnPoll);

	// Whether a request to an RPC declared GameFrameOnly has been held back, because the game world's
	// ATempoWorldSettings said it had to wait. TickInternal handles held requests first, once it no
	// longer does.
	bool HasHeldRequests() const { return !HeldRequestTags.IsEmpty(); }

	// Whether any request has a write or finish in flight, or responses queued behind one. A stream
	// asks its handler for its next response only once these drain. A stalled request (see
	// FinishWrites) does not count until its write completes.
	bool HasUnflushedWork() const;

	// Handles write and finish completions until no request has unflushed work, and nothing else: any
	// other event, such as a new request, is set aside for the next TickInternal or PumpEvents to handle
	// first, in order. So no new request is handled where this runs, such as inside the world tick
	// before its time has moved. A stream's handler is re-invoked when its last write completes, which
	// every streaming handler answers by registering interest and responding later (see
	// ITempoServiceProvider); one that responded at once would run here, and keep this from draining.
	// Like a FixedStep server tick, it waits as long as the writes take, up to Tempo.WriteStallTimeoutMs:
	// a write completes only once its client has room for it, and a client that dropped a stream without
	// cancelling it never makes room. Every request still unflushed then is marked stalled, and neither
	// this nor a FixedStep server tick waits on it again until its write completes.
	void FinishWrites();

protected:
	void Initialize();
	void Deinitialize();
	void Reinitialize();
	void TickInternal();

	void ActivateService(const FName& ServiceName, UObject* Object)
	{
		if (!Services.Contains(ServiceName) && Object->Implements<UTempoServiceProvider>() && !Server.IsValid())
		{
			// That service has not been registered, but we haven't started the server yet.
			// Maybe this object just tried to activate very early in engine initialization, and we can register it early?
			Cast<ITempoServiceProvider>(Object)->RegisterServices(*this);
		}
		for (const auto& RequestManager : RequestManagers)
		{
			if (RequestManager.Value->GetServiceName() == ServiceName)
			{
				RequestManager.Value->Activate(Object);
			}
		}
	}

	void DeactivateService(const FName& ServiceName)
	{
		for (const auto& RequestManager : RequestManagers)
		{
			if (RequestManager.Value->GetServiceName() == ServiceName)
			{
				RequestManager.Value->Deactivate();
			}
		}
	}

	// Credit: https://stackoverflow.com/a/44065093
	template <class...>
	struct False : std::bool_constant<false> { };

	// Zero-Handler case.
	template <class AsyncServiceType>
	static void RegisterHandlers(AsyncServiceType* Service)
	{
		static_assert(False<AsyncServiceType>{}, "RegisterHandlers must be called with at least one handler");
	}

	// One-Handler case.
	template <class AsyncServiceType, class HandlerType>
	void RegisterHandlers(const FName& ServiceName, AsyncServiceType* Service, HandlerType& Handler)
	{
		const int32 Tag = TagAllocator++;
		Handler.Init(Service);
		RequestManagers.Emplace(Tag, new TRequestManager<HandlerType>(Tag, ServiceName, Service, MakeShared<HandlerType>(MoveTemp(Handler))));
	}

	// Recursively register the handlers.
	template <class AsyncServiceType, class FirstHandlerType, class... RemainingHandlerTypes>
	void RegisterHandlers(const FName& ServiceName, AsyncServiceType* Service, FirstHandlerType& FirstHandler, RemainingHandlerTypes&... RemainingHandlers)
	{
		RegisterHandlers<AsyncServiceType, FirstHandlerType>(ServiceName, Service, FirstHandler);
		RegisterHandlers<AsyncServiceType, RemainingHandlerTypes...>(ServiceName, Service, RemainingHandlers...);
	}

	void HandleEventForTag(int32 Tag, bool bOk);

	bool bIsInitialized = false;

	bool ShouldHoldGameFrameOnlyRequests() const;

	TArray<int32> HeldRequestTags;

	// Events FinishWrites set aside, in the order they arrived.
	TArray<TPair<int32, bool>> DeferredEvents;
	void HandleDeferredEvents();

	// Requests whose write or finish outlasted Tempo.WriteStallTimeoutMs, which HasUnflushedWork ignores
	// until their next event.
	TSet<int32> StalledTags;
	static double GetWriteStallTimeoutSeconds();
	void MarkStalledWrites();

	// The game world the server pumps from, whose ATempoWorldSettings decides whether a GameFrameOnly
	// request must wait.
	TWeakObjectPtr<UWorld> GameWorld;

	int32 TagAllocator = 0;
	TMap<int32, TSharedPtr<FRequestManager>> RequestManagers;
	TMap<FName, TUniquePtr<grpc::Service>> Services;

	TUniquePtr<grpc::Server> Server;
	TUniquePtr<grpc::ServerCompletionQueue> CompletionQueue;

	// The Unix domain socket this server bound, to remove on shutdown. Empty for the Tcp transport.
	FString BoundSocketPath;

	FDelegateHandle OnPostEngineInitHandle;
	FDelegateHandle OnPostWorldInitializationHandle;
	FDelegateHandle OnWorldBeginPlayHandle;
	FDelegateHandle OnPreWorldFinishDestroyHandle;
	FDelegateHandle OnMovieSceneSequenceTickHandle;
};
