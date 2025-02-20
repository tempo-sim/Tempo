// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoZoneGraphUtils.h"

#include "TempoAgentsShared.h"

#include "ZoneGraphSubsystem.h"

FZoneGraphTagMask UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(const UObject* WorldContextObject, const TArray<FName>& TagNames)
{
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(WorldContextObject->GetWorld());
	if (ZoneGraphSubsystem == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("TempoZoneGraphUtils - GenerateTagMaskFromTagNames - Can't access ZoneGraphSubsystem."));
		return FZoneGraphTagMask::None;
	}

	FZoneGraphTagMask ZoneGraphTagMask;

	for (const auto& TagName : TagNames)
	{
		const FZoneGraphTag Tag = ZoneGraphSubsystem->GetTagByName(TagName);
		ZoneGraphTagMask.Add(Tag);
	}

	return ZoneGraphTagMask;
}

FZoneGraphTagFilter UTempoZoneGraphUtils::GenerateTagFilter(const UObject* WorldContextObject, const TArray<FName>& AnyTags, const TArray<FName>& AllTags, const TArray<FName>& NotTags)
{
	const FZoneGraphTagMask AnyTagsMask = UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(WorldContextObject, AnyTags);
	const FZoneGraphTagMask AllTagsMask = UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(WorldContextObject, AllTags);
	const FZoneGraphTagMask NotTagsMask = UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(WorldContextObject, NotTags);

	return FZoneGraphTagFilter{AnyTagsMask, AllTagsMask, NotTagsMask};
}
