// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include <grpcpp/grpcpp.h>

#include "CoreMinimal.h"

#include "TempoScriptingServer.generated.h"

/**
 * A request handler connects the gRPC pipes to accept, handle, and respond to requests to user callbacks.
 */
template <class ServiceType, class RequestType, class ResponseType>
struct TRequestHandler
{
	typedef ServiceType HandlerServiceType;
	typedef RequestType HandlerRequestType;
	typedef ResponseType HandlerResponseType;

	typedef typename TMemFunPtrType<false, ServiceType, void(grpc::ServerContext*, RequestType*, grpc::ServerAsyncResponseWriter<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)>::Type AcceptFuncType;
	
	TRequestHandler(AcceptFuncType AcceptFuncIn)
		: AcceptFunc(AcceptFuncIn)
	{
		static_assert(std::is_base_of_v<grpc::Service, ServiceType>);
	}
	
	void Init(ServiceType* Service)
	{
		AcceptDelegate.BindRaw(Service, AcceptFunc);
	}

	void AcceptRequests(grpc::ServerContext* Context, RequestType* Request, grpc::ServerAsyncResponseWriter<ResponseType>* Responder, grpc::ServerCompletionQueue* CompletionQueue, void* Tag)
	{
		check(AcceptDelegate.IsBound());
		AcceptDelegate.Execute(Context, Request, Responder, CompletionQueue, CompletionQueue, Tag);
	}

	grpc::Status HandleRequest(const RequestType& Request, ResponseType& Response)
	{
		check(HandleDelegate.IsBound());
		return HandleDelegate.Execute(Request, Response);
	}

	template<class UserObjectType, typename... VarTypes>
	TRequestHandler& BindUObject(UserObjectType* UserObject, typename TMemFunPtrType<false, UserObjectType, grpc::Status (const RequestType&, ResponseType&, VarTypes...)>::Type HandleFunction, VarTypes&&... Vars)
	{
		checkf(!HandleDelegate.IsBound(), TEXT("Handle delegate is already bound. Multiple handlers is not supported."));
		HandleDelegate.BindUObject(UserObject, HandleFunction, Forward<VarTypes>(Vars)...);
		return *this;
	}

	template<class UserObjectType, typename... VarTypes>
	TRequestHandler& BindUObject(UserObjectType* UserObject, typename TMemFunPtrType<true, UserObjectType, grpc::Status (const RequestType&, ResponseType&, VarTypes...)>::Type HandleFunction, VarTypes&&... Vars)
	{
		checkf(!HandleDelegate.IsBound(), TEXT("Handle delegate is already bound. Multiple handlers is not supported."));
		HandleDelegate.BindUObject(UserObject, HandleFunction, Forward<VarTypes>(Vars)...);
		return *this;
	}

	template<class UserObjectType, typename... VarTypes>
	TRequestHandler& BindRaw(UserObjectType* UserObject, typename TMemFunPtrType<false, UserObjectType, grpc::Status (const RequestType&, ResponseType&, VarTypes...)>::Type HandleFunction, VarTypes&&... Vars)
	{
		checkf(!HandleDelegate.IsBound(), TEXT("Handle delegate is already bound. Multiple handlers is not supported."));
		HandleDelegate.BindRaw(UserObject, HandleFunction, Forward<VarTypes>(Vars)...);
		return *this;
	}

	template<class UserObjectType, typename... VarTypes>
	TRequestHandler& BindRaw(UserObjectType* UserObject, typename TMemFunPtrType<true, UserObjectType, grpc::Status (const RequestType&, ResponseType&, VarTypes...)>::Type HandleFunction, VarTypes&&... Vars)
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
	TDelegate<grpc::Status (const RequestType&, ResponseType&)> HandleDelegate;
	TDelegate<void(grpc::ServerContext*, RequestType*, grpc::ServerAsyncResponseWriter<ResponseType>*, grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)> AcceptDelegate;
};

/**
 * A request manager manages the lifecycle of one gRPC request.
 * This interface allows ownership by the UTempoScriptingServer without visibility into the specific handler types.
 */
struct FRequestManager
{
	virtual ~FRequestManager() = default;
	enum EState { UNINITIALIZED, REQUESTED, RESPONDED };
	virtual EState GetState() const = 0;
	virtual void Init(grpc::ServerCompletionQueue* CompletionQueue) = 0;
	virtual void HandleAndRespond() = 0;
	virtual FRequestManager* Duplicate(int32 NewTag) const = 0;
};

template <class HandlerType>
class TRequestManager: public FRequestManager {
public:
	TRequestManager(int32 TagIn, const typename HandlerType::HandlerServiceType* ServiceIn, TSharedPtr<HandlerType> HandlerIn)
		: State(UNINITIALIZED), Tag(TagIn), Handler(HandlerIn), Service(ServiceIn), Responder(&Context) {}
	
	virtual EState GetState() const override { return State; }
	
	virtual void Init(grpc::ServerCompletionQueue* CompletionQueue) override
	{
		check(State == UNINITIALIZED);
		Handler->AcceptRequests(&Context, &Request, &Responder, CompletionQueue, &Tag);
		State = REQUESTED;
	}

	virtual void HandleAndRespond() override
	{
		check(State == REQUESTED);
		const grpc::Status Result = Handler->HandleRequest(Request, Response);
		Responder.Finish(Response, Result, &Tag);
		State = RESPONDED;
	}

	virtual FRequestManager* Duplicate(int32 NewTag) const override
	{
		return new TRequestManager(NewTag, Service, Handler);
	}

private:
	EState State;
	int32 Tag;
	const TSharedPtr<HandlerType> Handler;
	const typename HandlerType::HandlerServiceType* Service;
	typename HandlerType::HandlerRequestType Request;
	typename HandlerType::HandlerResponseType Response;
	grpc::ServerContext Context;
	grpc::ServerAsyncResponseWriter<typename HandlerType::HandlerResponseType> Responder;
};

/**
 * Hosts a gRPC server and supports registering arbitrary gRPC services and handlers.
 */
UCLASS()
class TEMPOSCRIPTING_API UTempoScriptingServer : public UObject, public FTickableGameObject
{
	GENERATED_BODY()
public:
	UTempoScriptingServer();
	UTempoScriptingServer(FVTableHelper& Helper);
	virtual ~UTempoScriptingServer() override;

	virtual void Initialize(int32 Port);
	virtual void Deinitialize();
	
	virtual bool IsTickable() const override { return bIsInitialized; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { return GetStatID(); }
	
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

	void HandleEventForTag(int32 Tag);
	
	UPROPERTY()
	bool bIsInitialized = false;

	int32 TagAllocator = 0;
	TMap<int32, TUniquePtr<FRequestManager>> RequestManagers;
	TArray<TUniquePtr<grpc::Service>> Services;
	
	TUniquePtr<grpc::Server> Server;
	TUniquePtr<grpc::ServerCompletionQueue> CompletionQueue;
};
