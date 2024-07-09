// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IPluginManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine.h"
#include "EngineUtils.h"
#include "PointCloudImpl.h"

namespace RuleProcessorTestConstants
{
	static const FString DefaultTestDataFile = TEXT("CitySubset.psv");
}

/** A little helper class that RAIIs an object and calls MarkPendingKill on it */
template<class T> class FAssetDeleter
{
public:
	explicit FAssetDeleter(T* Me)
	{
		Ptr = Me;
	}

	FAssetDeleter(const FAssetDeleter& InOther) = delete;

	FAssetDeleter(FAssetDeleter&& InOther)
	{
		Swap(Ptr, InOther.Ptr);
	}

	FAssetDeleter& operator=(const FAssetDeleter& InOther) = delete;

	FAssetDeleter& operator=(FAssetDeleter&& InOther)
	{
		Swap(Ptr, InOther.Ptr);
	}

	~FAssetDeleter()
	{
		if (Ptr != nullptr)
		{
			Ptr->MarkAsGarbage();
		}
	}

	T* Get() const
	{
		return Ptr;
	}
private:
	T* Ptr = nullptr;
};

namespace
{
	FString PathToTestData(const FString& Name)
	{
		FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("RuleProcessor"))->GetBaseDir() + TEXT("\\Content\\TestingData\\") + Name;
		return ContentDir;
	}

	// A little bit of boiler plate code to create a test UPointCloudAsset in the transient package
	UPointCloud* CreateTestAsset()
	{
		UPackage* Package = GetTransientPackage();

		if (Package == nullptr)
		{
			return nullptr;
		}

		UPointCloudImpl* TestAsset = NewObject<UPointCloudImpl>(Package, UPointCloudImpl::StaticClass(), *FString(TEXT("TestingAsset")), EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);

		if (TestAsset == nullptr)
		{
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(TestAsset);
		TestAsset->MarkPackageDirty();

		return TestAsset;
	}

	// Copy of the hidden method GetAnyGameWorld() in AutomationCommon.cpp.
	// Marked as temporary there, hence, this one is temporary, too.
	UWorld* GetTestWorld() {
		const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
		for (const FWorldContext& Context : WorldContexts) {
			if (((Context.WorldType == EWorldType::PIE) || (Context.WorldType == EWorldType::Game))
				&& (Context.World() != nullptr)) {
				return Context.World();
			}
		}

		return nullptr;
	}
}
