// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include <grpcpp/grpcpp.h>

#include "CoreMinimal.h"

#include "TempoScriptingServer.generated.h"

template <class ResponseType>
using TResponseDelegate = TDelegate<void(const ResponseType&, grpc::Status)>;

namespace grpc
{
	static const Status Status_OK;
}

template <class ServiceType, class RequestType, class ResponseType, template <class> class ResponderType>
struct TRequestHandler
{
	typedef ServiceType HandlerServiceType;
	typedef RequestType HandlerRequestType;
	typedef ResponseType HandlerResponseType;
	typedef ResponderType<ResponseType> HandlerResponderType;

	typedef typename TMemFunPtrType<false, ServiceType, void(grpc::ServerContext*, RequestType*, ResponderType<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)>::Type AcceptFuncType;

	TRequestHandler(AcceptFuncType&& AcceptFuncIn)
		: AcceptFunc(AcceptFuncIn)
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
		check(HandleDelegate.IsBound());
		HandleDelegate.Execute(Request, ResponseDelegate);
	}

	template<class UserObjectType, typename... VarTypes>
	TRequestHandler& BindUObject(UserObjectType* UserObject, typename TMemFunPtrType<false, UserObjectType, void(const RequestType&, const TResponseDelegate<ResponseType>&, VarTypes...)>::Type HandleFunction, VarTypes&&... Vars)
	{
		checkf(!HandleDelegate.IsBound(), TEXT("Handle delegate is already bound. Multiple handlers is not supported."));
		HandleDelegate.BindUObject(UserObject, HandleFunction, Forward<VarTypes>(Vars)...);
		return *this;
	}

	template<class UserObjectType, typename... VarTypes>
	TRequestHandler& BindUObject(UserObjectType* UserObject, typename TMemFunPtrType<true, UserObjectType, void(const RequestType&, const TResponseDelegate<ResponseType>&, VarTypes...)>::Type HandleFunction, VarTypes&&... Vars)
	{
		checkf(!HandleDelegate.IsBound(), TEXT("Handle delegate is already bound. Multiple handlers is not supported."));
		HandleDelegate.BindUObject(UserObject, HandleFunction, Forward<VarTypes>(Vars)...);
		return *this;
	}

	template<class UserObjectType, typename... VarTypes>
	TRequestHandler& BindRaw(UserObjectType* UserObject, typename TMemFunPtrType<false, UserObjectType, void(const RequestType&, const TResponseDelegate<ResponseType>&, VarTypes...)>::Type HandleFunction, VarTypes&&... Vars)
	{
		checkf(!HandleDelegate.IsBound(), TEXT("Handle delegate is already bound. Multiple handlers is not supported."));
		HandleDelegate.BindRaw(UserObject, HandleFunction, Forward<VarTypes>(Vars)...);
		return *this;
	}

	template<class UserObjectType, typename... VarTypes>
	TRequestHandler& BindRaw(UserObjectType* UserObject, typename TMemFunPtrType<true, UserObjectType, void(const RequestType&, const TResponseDelegate<ResponseType>&, VarTypes...)>::Type HandleFunction, VarTypes&&... Vars)
	{
		checkf(!HandleDelegate.IsBound(), TEXT("Handle delegate is already bound. Multiple handlers is not supported."));
		HandleDelegate.BindRaw(UserObject, HandleFunction, Forward<VarTypes>(Vars)...);
		return *this;
	}

	template<typename FunctorType, typename... VarTypes>
	TRequestHandler& BindLambda(FunctorType&& InFunctor, VarTypes&&... Vars)
	{
		checkf(!HandleDelegate.IsBound(), TEXT("Handle delegate is already bound. Multiple handlers is not supported."));
		HandleDelegate.BindLambda(InFunctor, Forward<VarTypes>(Vars)...);
		return *this;
	}

private:
	AcceptFuncType AcceptFunc;
	TDelegate<void(const RequestType&, const TResponseDelegate<ResponseType>&)> HandleDelegate;
	TDelegate<void(grpc::ServerContext*, RequestType*, ResponderType<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)> AcceptDelegate;
};

template <class ServiceType, class RequestType, class ResponseType>
using TSimpleRequestHandler = TRequestHandler<ServiceType, RequestType, ResponseType, grpc::ServerAsyncResponseWriter>;

template <class ServiceType, class RequestType, class ResponseType>
using TStreamingRequestHandler = TRequestHandler<ServiceType, RequestType, ResponseType, grpc::ServerAsyncWriter>;

/**
 * A request manager manages the lifecycle of one gRPC request.
 * This interface allows ownership by the UTempoScriptingServer without visibility into the concrete handler types.
 */
struct FRequestManager : TSharedFromThis<FRequestManager>
{
	virtual ~FRequestManager() = default;
	enum EState { UNINITIALIZED, REQUESTED, HANDLING, RESPONDING, FINISHING };
	virtual EState GetState() const = 0;
	virtual void Init(grpc::ServerCompletionQueue* CompletionQueue) = 0;
	virtual void HandleAndRespond() = 0;
	virtual FRequestManager* Duplicate(int32 NewTag) const = 0;
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

template <class ServiceType, class RequestType, class ResponseType>
class TRequestManager<TSimpleRequestHandler<ServiceType, RequestType, ResponseType>> :
	public TRequestManagerBase<TSimpleRequestHandler<ServiceType, RequestType, ResponseType>>
{
	using TRequestManagerBase<TSimpleRequestHandler<ServiceType, RequestType, ResponseType>>::TRequestManagerBase;
	using Base = TRequestManagerBase<TSimpleRequestHandler<ServiceType, RequestType, ResponseType>>;

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
};

template <class ServiceType, class RequestType, class ResponseType>
class TRequestManager<TStreamingRequestHandler<ServiceType, RequestType, ResponseType>> :
	public TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType>>
{
	using TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType>>::TRequestManagerBase;
	using Base = TRequestManagerBase<TStreamingRequestHandler<ServiceType, RequestType, ResponseType>>;

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
};

/**
 * Hosts a gRPC server and supports registering arbitrary gRPC services and handlers.
 */
UCLASS()
class TEMPOSCRIPTING_API UTempoScriptingServer : public UObject
{
	GENERATED_BODY()
public:
	UTempoScriptingServer();
	UTempoScriptingServer(FVTableHelper& Helper);
	virtual ~UTempoScriptingServer() override;

	virtual void Initialize(int32 Port);
	virtual void Deinitialize();

	// The server must be explicitly Ticked. Its owner may choose the most appropriate time within the frame to do so.
	virtual void Tick(float DeltaTime);
	
	template <class ServiceType, class... HandlerTypes>
	void RegisterService(HandlerTypes... Handlers)
	{
		ServiceType* Service = new ServiceType();
		Services.Emplace(Service);
		RegisterHandlers<ServiceType, HandlerTypes...>(Service, Handlers...);
	}

private:
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
	
	UPROPERTY()
	bool bIsInitialized = false;

	int32 TagAllocator = 0;
	TMap<int32, TSharedPtr<FRequestManager>> RequestManagers;
	TArray<TUniquePtr<grpc::Service>> Services;
	
	TUniquePtr<grpc::Server> Server;
	TUniquePtr<grpc::ServerCompletionQueue> CompletionQueue;
};
