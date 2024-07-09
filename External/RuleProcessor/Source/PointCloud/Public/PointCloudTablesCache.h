// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "HAL/CriticalSection.h"
#include "Containers/LruCache.h"
#include "PointCloudConfig.h"

/** Convenience class to hide multithreading safety */
class FPointCloudTemporaryTablesCache
{
public:
	FPointCloudTemporaryTablesCache();

	bool Contains(const FString& InKey) const;
	FString GetFromCache(const FString& InKey, int32* OutCacheHits = nullptr);
	FString AddToCache(const FString& InKey, const FString& InName);
	FString RemoveLeastRecentNotThreadSafe();

#if defined RULEPROCESSOR_ENABLE_LOGGING
	const TMap<FString, int32>& GetCacheMisses() const { return CacheMisses; }
#endif

private:
	//  Least Recently used cache containing the names of temporary tables created by GetTemporaryAttributeTable or GetTemporaryQueryTable
	// The size of this cache is controlled by GetTemporaryTableCacheSize, once this number of tables has been created, the least recently used will be 
	// evicted from memory
	TLruCache<FString, FString> TemporaryTables;

	// A lock to protect access to this struct's members
	mutable FCriticalSection CacheLock;

#if defined RULEPROCESSOR_ENABLE_LOGGING

	// Map between Queries and cache miss counts
	TMap<FString, int32> CacheMisses;

#endif

	// Map between Queries and cache miss counts
	TMap<FString, int32> CacheHits;

	int32 CacheSize;

	int32 EjectedTablesCount;
};