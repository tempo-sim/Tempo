// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSNode.h"

#include "TempoScriptingServer.h"

template <typename ServiceType>
using TScriptingServiceDelegate = TDelegate<void(const typename ServiceType::Request&, const TResponseDelegate<typename ServiceType::Response>&)>;

template <typename ServiceType>
static void BindScriptingServiceToROS(UTempoROSNode* ROSNode, const FString& Name, TScriptingServiceDelegate<ServiceType> ScriptingCallback)
{
	ROSNode->AddService<ServiceType>(Name, TROSServiceDelegate<ServiceType>::CreateLambda([ScriptingCallback, Name](const typename ServiceType::Request& Request)
	{
		TOptional<typename ServiceType::Response> Response;
		ScriptingCallback.ExecuteIfBound(Request, TResponseDelegate<typename ServiceType::Response>::CreateLambda([&Response](const typename ServiceType::Response& ResponseIn, grpc::Status Status)
		{
			Response = ResponseIn;
		}));
		checkf(Response.IsSet(), TEXT("Service %s did not generate response immediately"), *Name);
		return Response.GetValue();
	}));
}

template <typename ServiceType, typename ScriptingClass>
static void BindScriptingServiceToROS(UTempoROSNode* ROSNode, const FString& Name, ScriptingClass* ScriptingObj, typename TScriptingServiceDelegate<ServiceType>::template TMethodPtr<ScriptingClass> ScriptingMethod)
{
	BindScriptingServiceToROS<ServiceType>(ROSNode, Name, TScriptingServiceDelegate<ServiceType>::CreateUObject(ScriptingObj, ScriptingMethod));
}

template <typename ServiceType, typename ScriptingClass>
static void BindScriptingServiceToROS(UTempoROSNode* ROSNode, const FString& Name, ScriptingClass* ScriptingObj, typename TScriptingServiceDelegate<ServiceType>::template TConstMethodPtr<ScriptingClass> ScriptingMethod)
{
	BindScriptingServiceToROS<ServiceType>(ROSNode, Name, TScriptingServiceDelegate<ServiceType>::CreateUObject(ScriptingObj, ScriptingMethod));
}
