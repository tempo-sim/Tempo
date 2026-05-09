// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSNode.h"

#include "TempoServer.h"

template <typename ServiceType>
using TTempoServiceDelegate = TDelegate<void(const typename ServiceType::Request&, const TResponseDelegate<typename ServiceType::Response>&)>;

template <typename ServiceType>
static void BindServiceToROS(UTempoROSNode* ROSNode, const FString& Name, TTempoServiceDelegate<ServiceType> Handler)
{
	ROSNode->AddService<ServiceType>(Name, TROSServiceDelegate<ServiceType>::CreateLambda([Handler, Name](const typename ServiceType::Request& Request)
	{
		TOptional<typename ServiceType::Response> Response;
		Handler.ExecuteIfBound(Request, TResponseDelegate<typename ServiceType::Response>::CreateLambda([&Response](const typename ServiceType::Response& ResponseIn, grpc::Status Status)
		{
			Response = ResponseIn;
		}));
		checkf(Response.IsSet(), TEXT("Service %s did not generate response immediately"), *Name);
		return Response.GetValue();
	}));
}

template <typename ServiceType, typename UserClass>
static void BindServiceToROS(UTempoROSNode* ROSNode, const FString& Name, UserClass* UserObject, typename TTempoServiceDelegate<ServiceType>::template TMethodPtr<UserClass> Method)
{
	BindServiceToROS<ServiceType>(ROSNode, Name, TTempoServiceDelegate<ServiceType>::CreateUObject(UserObject, Method));
}

template <typename ServiceType, typename UserClass>
static void BindServiceToROS(UTempoROSNode* ROSNode, const FString& Name, UserClass* UserObject, typename TTempoServiceDelegate<ServiceType>::template TConstMethodPtr<UserClass> Method)
{
	BindServiceToROS<ServiceType>(ROSNode, Name, TTempoServiceDelegate<ServiceType>::CreateUObject(UserObject, Method));
}
