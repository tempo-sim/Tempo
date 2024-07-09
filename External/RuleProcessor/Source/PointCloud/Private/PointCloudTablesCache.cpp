// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudTablesCache.h"
#include "PointCloudImpl.h"

FPointCloudTemporaryTablesCache::FPointCloudTemporaryTablesCache()
	: TemporaryTables(UPointCloudImpl::GetTemporaryTableCacheSize() + 1)
	, CacheSize(UPointCloudImpl::GetTemporaryTableCacheSize())
	, EjectedTablesCount(0)
{

}

bool FPointCloudTemporaryTablesCache::Contains(const FString& InKey) const
{
	CacheLock.Lock();
	bool bFound = TemporaryTables.Contains(InKey);
	CacheLock.Unlock();
	return bFound;
}

FString FPointCloudTemporaryTablesCache::GetFromCache(const FString& InKey, int32* OutCachedHits)
{
	FString CachedTableName;
	int32 CacheHitCount = 0;

	CacheLock.Lock();
	if (TemporaryTables.Contains(InKey))
	{
		CachedTableName = TemporaryTables.FindAndTouchRef(InKey);
		CacheHitCount = ++CacheHits[CachedTableName];
	}
	CacheLock.Unlock();

	if (OutCachedHits)
	{
		*OutCachedHits = CacheHitCount;
	}

	return CachedTableName;
}

FString FPointCloudTemporaryTablesCache::AddToCache(const FString& InKey, const FString& InName)
{
	FString TableToEject;

	CacheLock.Lock();
	TemporaryTables.Add(InKey, InName);

	// Add to cache hits
	if (CacheHits.Contains(InName))
	{
		CacheHits[InName] = 1;
	}
	else
	{
		CacheHits.Add(InName, 1);
	}

	if (TemporaryTables.Num() > CacheSize)
	{
		TableToEject = *TemporaryTables.RemoveLeastRecent();
		++EjectedTablesCount;
	}

#if defined RULEPROCESSOR_ENABLE_LOGGING
	// Add to cache misses as well
	if (CacheMisses.Contains(InName))
	{
		CacheMisses[InName]++;
	}
	else
	{
		CacheMisses.Add(InName, 1);
	}
#endif

	CacheLock.Unlock();

	return TableToEject;
}

FString FPointCloudTemporaryTablesCache::RemoveLeastRecentNotThreadSafe()
{
	if (TemporaryTables.Num() > 0)
	{
		FString TableToDrop = *TemporaryTables.RemoveLeastRecent();

		// Remove from cache
		CacheHits.Remove(TableToDrop);

		return TableToDrop;
	}
	else
	{
		return FString();
	}
}