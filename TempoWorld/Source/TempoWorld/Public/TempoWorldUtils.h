// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "AssetRegistry/AssetRegistryModule.h"

// GetSubClassWithName used to live here; it now lives in TempoCore so every plugin can resolve
// a client-supplied class name the same way. Included so existing users of this header still see it.
#include "TempoClassUtils.h"

AActor* GetActorWithName(const UWorld* World, const FString& Name);

UObject* GetAssetByPath(const FString& AssetPath);

template <typename T = UActorComponent>
T* GetComponentWithName(const AActor* Actor, const FString& Name)
{
	TArray<T*> Components;
	Actor->GetComponents<T>(Components);
	for (T* Component : Components)
	{
		if (Component->GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Component;
		}
	}

	return nullptr;
}
