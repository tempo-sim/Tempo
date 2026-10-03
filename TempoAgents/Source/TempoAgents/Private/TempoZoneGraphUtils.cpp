// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoZoneGraphUtils.h"

#include "TempoAgents.h"

#include "ZoneGraphSubsystem.h"

FZoneGraphTag UTempoZoneGraphUtils::GetTagByName(const UObject* WorldContextObject, FName TagName)
{
	const UZoneGraphSubsystem* ZoneGraphSubsystem = WorldContextObject ? UWorld::GetSubsystem<UZoneGraphSubsystem>(WorldContextObject->GetWorld()) : nullptr;
	return ZoneGraphSubsystem ? ZoneGraphSubsystem->GetTagByName(TagName) : FZoneGraphTag::None;
}

FName UTempoZoneGraphUtils::GetTagName(const UObject* WorldContextObject, FZoneGraphTag Tag)
{
	const UZoneGraphSubsystem* ZoneGraphSubsystem = WorldContextObject ? UWorld::GetSubsystem<UZoneGraphSubsystem>(WorldContextObject->GetWorld()) : nullptr;
	return ZoneGraphSubsystem ? ZoneGraphSubsystem->GetTagName(Tag) : FName();
}

TArray<FName> UTempoZoneGraphUtils::GetTagNamesFromTagMask(const UObject* WorldContextObject, const FZoneGraphTagMask& TagMask)
{
	TArray<FName> TagNames;
	if (const UZoneGraphSubsystem* ZoneGraphSubsystem = WorldContextObject ? UWorld::GetSubsystem<UZoneGraphSubsystem>(WorldContextObject->GetWorld()) : nullptr)
	{
		for (const FZoneGraphTagInfo& TagInfo : ZoneGraphSubsystem->GetTagInfos())
		{
			if (TagMask.Contains(TagInfo.Tag))
			{
				TagNames.Add(TagInfo.Name);
			}
		}
	}
	return TagNames;
}

FZoneGraphTagMask UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(const UObject* WorldContextObject, const TArray<FName>& TagNames)
{
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(WorldContextObject->GetWorld());
	if (ZoneGraphSubsystem == nullptr)
	{
		UE_LOG(LogTempoAgents, Error, TEXT("TempoZoneGraphUtils - GenerateTagMaskFromTagNames - Can't access ZoneGraphSubsystem."));
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

FTempoZoneGraphTagFilter UTempoZoneGraphUtils::GenerateTagFilter(const UObject* WorldContextObject, const TArray<FName>& AnyTags, const TArray<FName>& AllTags, const TArray<FName>& NotTags)
{
	const FZoneGraphTagMask AnyTagsMask = UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(WorldContextObject, AnyTags);
	const FZoneGraphTagMask AllTagsMask = UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(WorldContextObject, AllTags);
	const FZoneGraphTagMask NotTagsMask = UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(WorldContextObject, NotTags);

	FTempoZoneGraphTagFilter TagFilter;
	TagFilter.AnyTags = AnyTagsMask;
	TagFilter.AllTags = AllTagsMask;
	TagFilter.NotTags = NotTagsMask;
	return TagFilter;
}

TArray<FTempoZoneGraphTagFilter> UTempoZoneGraphUtils::GetLaneTagFilters(const FMassTrafficLanePriorityFilters& LanePriorityFilters)
{
	return TArray<FTempoZoneGraphTagFilter>(LanePriorityFilters.LaneTagFilters);
}

void UTempoZoneGraphUtils::SetLaneTagFilters(FMassTrafficLanePriorityFilters& LanePriorityFilters, const TArray<FTempoZoneGraphTagFilter>& LaneTagFilters)
{
	LanePriorityFilters.LaneTagFilters = TArray<FZoneGraphTagFilter>(LaneTagFilters);
}
