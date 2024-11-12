// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include <grpcpp/grpcpp.h>

#include "CoreMinimal.h"
#include "TempoScripting.h"
#include "grpcpp/impl/service_type.h"

template <typename MessageType>
struct TServiceTypeTraits
{
	static FName ServiceId;
};

template <typename ServiceType>
FName TServiceTypeTraits<ServiceType>::ServiceId = FName(NAME_None);

#define DEFINE_TEMPO_SERVICE_TYPE_TRAITS(ServiceType) \
template <> inline FName TServiceTypeTraits<ServiceType>::ServiceId = FName(#ServiceType);

template <class ResponseType>
using TResponseDelegate = TDelegate<void(const ResponseType&, grpc::Status)>;

namespace grpc
{
	static const Status Status_OK;
}

template <class ServiceType, class RequestType, class ResponseType, template <class> class ResponderType, class UserObjectType, bool Const>
struct TRequestHandler
{
	enum EBindType
	{
		UObject = 1,
		Raw = 2,
		Lambda = 3
	};
	typedef ServiceType HandlerServiceType;
	typedef RequestType HandlerRequestType;
	typedef ResponseType HandlerResponseType;
	typedef ResponderType<ResponseType> HandlerResponderType;

	typedef typename TMemFunPtrType<false, ServiceType, void(grpc::ServerContext*, RequestType*, ResponderType<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)>::Type AcceptFuncType;
	typedef typename TMemFunPtrType<Const, UserObjectType, void(const RequestType&, const TResponseDelegate<ResponseType>&)>::Type HandleFuncType;

	TRequestHandler(AcceptFuncType AcceptFuncIn, HandleFuncType HandleFuncIn)
		: AcceptFunc(AcceptFuncIn), HandleFunc(HandleFuncIn)
	{
		static_assert(std::is_base_of_v<grpc::Service, ServiceType>);
	}
	
	void Init(ServiceType* Service)
	{
		AcceptDelegate.BindRaw(Service, AcceptFunc);
	}

	void AcceptRequests(grpc::ServerContext* Context, RequestType* Request, ResponderType<ResponseType>* Responder, grpc::ServerCompletionQueue* CompletionQueue, void* Tag)
	{
		check(AcceptDelegate.IsBound());
		AcceptDelegate.Execute(Context, Request, Responder, CompletionQueue, CompletionQueue, Tag);
	}
	
	void HandleRequest(const RequestType& Request, const TResponseDelegate<ResponseType>& ResponseDelegate)
	{
		if (!HandleDelegate.ExecuteIfBound(Request, ResponseDelegate))
		{
			ResponseDelegate.ExecuteIfBound(ResponseType(), grpc::Status(grpc::UNAVAILABLE, "No handler registered for RPC"));
		}
	}

	void BindObjectToService(UserObjectType* UserObject)
	{
		if (!ensureAlwaysMsgf(!UserObject->HasAnyFlags(RF_ClassDefaultObject), TEXT("Bound called on CDO! Do not call BindObjectToService from RegisterScriptingServices.")))
		{
			return;
		}
		if (HandleDelegate.IsBound() && BindType == EBindType::Lambda)
		{
			return;
		}
		ensureAlwaysMsgf(!HandleDelegate.IsBound(), TEXT("Handle delegate was already bound."));
		HandleDelegate.BindUObject(UserObject, HandleFunc);
	}

	template<typename FunctorType>
	TRequestHandler& BindLambda(FunctorType&& InFunctor)
	{
		ensureAlwaysMsgf(!HandleDelegate.IsBound(), TEXT("Handle delegate was already bound."));
		HandleDelegate.BindLambda(InFunctor);
		BindType = EBindType::Lambda;
		return *this;
	}

private:
	AcceptFuncType AcceptFunc;
	HandleFuncType HandleFunc;
	TDelegate<void(const RequestType&, const TResponseDelegate<ResponseType>&)> HandleDelegate;
	TDelegate<void(grpc::ServerContext*, RequestType*, ResponderType<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)> AcceptDelegate;
	EBindType BindType = EBindType::UObject;
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
 * This interface allows ownership by the FTempoScriptingServer without visibility into the concrete handler types.
 */
struct FRequestManager : TSharedFromThis<FRequestManager>
{
	virtual ~FRequestManager() = default;
	enum EState { UNINITIALIZED, REQUESTED, HANDLING, RESPONDING, FINISHING };
	virtual EState GetState() const = 0;
	virtual void Init(grpc::ServerCompletionQueue* CompletionQueue) = 0;
	virtual void HandleAndRespond() = 0;
	virtual FRequestManager* Duplicate(int32 NewTag) const = 0;
	virtual const grpc::Service* GetService() const = 0;
	virtual void BindObjectToService(UObject* Object) = 0;
};

template <class HandlerType>
class TRequestManagerBase: public FRequestManager {
public:
	TRequestManagerBase(int32 TagIn, const typename HandlerType::HandlerServiceType* ServiceIn, TSharedPtr<HandlerType> HandlerIn)
		: State(UNINITIALIZED), Tag(TagIn), Handler(HandlerIn), Service(ServiceIn), Responder(&Context) {}
	
	virtual EState GetState() const override { return State; }
	
	virtual void Init(grpc::ServerCompletionQueue* CompletionQueue) override
	{
		check(State == UNINITIALIZED);
		Handler->AcceptRequests(&Context, &Request, &Responder, CompletionQueue, &Tag);
		State = REQUESTED;
	}

	virtual const grpc::Service* GetService() const override
	{
		return Service;
	}

protected:
	EState State;
	int32 Tag;
	const TSharedPtr<HandlerType> Handler;
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
		return new TRequestManager(NewTag, Base::Service, Base::Handler);
	}

	virtual void BindObjectToService(UObject* Object) override
	{
		Base::Handler->BindObjectToService(CastChecked<UserObjectType>(Object));
	}
};

template <class ServiceType, class RequestType, class ResponseType, class UserObjectType, bool Const>
class TRequestManager<TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>> :
	public TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>
{
	using TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>::TRequestManagerBase;
	using Base = TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType, UserObjectType, Const>>;

public:
	virtual void HandleAndRespond() override
	{
		check(Base::State == FRequestManager::EState::REQUESTED || Base::State == FRequestManager::EState::RESPONDING);
		Base::ResponseDelegate = TResponseDelegate<ResponseType>::CreateSPLambda(static_cast<FRequestManager*>(this), [this](const ResponseType& Response, grpc::Status Result)
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
		});
		Base::State = FRequestManager::EState::HANDLING;
		Base::Handler->HandleRequest(Base::Request, Base::ResponseDelegate);
	}

	virtual FRequestManager* Duplicate(int32 NewTag) const override
	{
		return new TRequestManager(NewTag, Base::Service, Base::Handler);
	}

	virtual void BindObjectToService(UObject* Object) override
	{
		Base::Handler->BindObjectToService(CastChecked<UserObjectType>(Object));
	}
};

/**
 * Hosts a gRPC server and supports registering arbitrary gRPC services and handlers.
 */
class TEMPOSCRIPTING_API FTempoScriptingServer : public FTickableGameObject
{
public:
	FTempoScriptingServer();
	virtual ~FTempoScriptingServer() override;

	static FTempoScriptingServer& Get();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	template <class ServiceType, class... HandlerTypes>
	void RegisterService(HandlerTypes... Handlers)
	{
		const FName ServiceId = TServiceTypeTraits<ServiceType>::ServiceId;
		checkf(ServiceId != NAME_None, TEXT("Service type traits not defined. Use DEFINE_TEMPO_SERVICE_TYPE_TRAITS."));
		ServiceType* Service = new ServiceType();
		Services.Emplace(ServiceId, Service);
		RegisterHandlers<ServiceType, HandlerTypes...>(Service, Handlers...);
	}

	template <class ServiceType, class UserObjectType>
	void BindObjectToService(UserObjectType* Object)
	{
		const FName ServiceId = TServiceTypeTraits<ServiceType>::ServiceId;
		checkf(ServiceId != NAME_None, TEXT("Service type traits not defined. Use DEFINE_TEMPO_SERVICE_TYPE_TRAITS."));
		const grpc::Service* Service = Services.Find(ServiceId)->Get();
		for (const auto& RequestManager : RequestManagers)
		{
			if (RequestManager.Value->GetService() == Service)
			{
				RequestManager.Value->BindObjectToService(Object);
			}
		}
	}

protected:
	void Initialize();
	void Deinitialize();
	void Reinitialize();
	void TickInternal(float DeltaTime);
	
	// Credit: https://stackoverflow.com/a/44065093
	template <class...>
	struct False : std::bool_constant<false> { };
	
	// Zero-Handler case.
	template <class ServiceType>
	static void RegisterHandlers(ServiceType* Service)
	{
		static_assert(False<ServiceType>{}, "RegisterHandlers must be called with at least one handler");
	}
	
	// One-Handler case.
	template <class ServiceType, class HandlerType>
	void RegisterHandlers(ServiceType* Service, HandlerType& Handler)
	{
		const int32 Tag = TagAllocator++;
		Handler.Init(Service);
		RequestManagers.Emplace(Tag, new TRequestManager<HandlerType>(Tag, Service, MakeShared<HandlerType>(MoveTemp(Handler))));
	}
	
	// Recursively register the handlers.
	template <class ServiceType, class FirstHandlerType, class... RemainingHandlerTypes>
	void RegisterHandlers(ServiceType* Service, FirstHandlerType& FirstHandler, RemainingHandlerTypes&... RemainingHandlers)
	{
		RegisterHandlers<ServiceType, FirstHandlerType>(Service, FirstHandler);
		RegisterHandlers<ServiceType, RemainingHandlerTypes...>(Service, RemainingHandlers...);
	}

	void HandleEventForTag(int32 Tag, bool bOk);

	bool bIsInitialized = false;

	int32 TagAllocator = 0;
	TMap<int32, TSharedPtr<FRequestManager>> RequestManagers;
	TMap<FName, TUniquePtr<grpc::Service>> Services;

	TUniquePtr<grpc::Server> Server;
	TUniquePtr<grpc::ServerCompletionQueue> CompletionQueue;

	FDelegateHandle OnPostEngineInitHandle;
	FDelegateHandle OnPostWorldInitializationHandle;
	FDelegateHandle OnWorldBeginPlayHandle;
	FDelegateHandle OnPreWorldFinishDestroyHandle;
	FDelegateHandle OnMovieSceneSequenceTickHandle;
};
