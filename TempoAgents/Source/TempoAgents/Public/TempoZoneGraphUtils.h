// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassTrafficTypes.h"
#include "ZoneGraphTypes.h"

#include "TempoZoneGraphUtils.generated.h"

// An FZoneGraphTagFilter for Blueprint, which the engine's is not visible to. It passes if any of the
// AnyTags, all of the AllTags and none of the NotTags are present, and converts to the engine's
// wherever C++ expects one.
USTRUCT(BlueprintType)
struct TEMPOAGENTS_API FTempoZoneGraphTagFilter
{
	GENERATED_BODY()

	FTempoZoneGraphTagFilter() = default;

	FTempoZoneGraphTagFilter(const FZoneGraphTagFilter& TagFilter)
		: AnyTags(TagFilter.AnyTags), AllTags(TagFilter.AllTags), NotTags(TagFilter.NotTags) {}

	operator FZoneGraphTagFilter() const
	{
		FZoneGraphTagFilter TagFilter;
		TagFilter.AnyTags = AnyTags;
		TagFilter.AllTags = AllTags;
		TagFilter.NotTags = NotTags;
		return TagFilter;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone")
	FZoneGraphTagMask AnyTags = FZoneGraphTagMask::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone")
	FZoneGraphTagMask AllTags = FZoneGraphTagMask::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone")
	FZoneGraphTagMask NotTags = FZoneGraphTagMask::None;
};

UCLASS()
class TEMPOAGENTS_API UTempoZoneGraphUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// UZoneGraphSubsystem's tag queries, which the engine does not expose to Blueprint.
	UFUNCTION(BlueprintCallable, Category="TempoZoneGraphUtils", meta=(WorldContext="WorldContextObject"))
	static FZoneGraphTag GetTagByName(const UObject* WorldContextObject, FName TagName);

	UFUNCTION(BlueprintCallable, Category="TempoZoneGraphUtils", meta=(WorldContext="WorldContextObject"))
	static FName GetTagName(const UObject* WorldContextObject, FZoneGraphTag Tag);

	UFUNCTION(BlueprintCallable, Category="TempoZoneGraphUtils", meta=(WorldContext="WorldContextObject"))
	static TArray<FName> GetTagNamesFromTagMask(const UObject* WorldContextObject, const FZoneGraphTagMask& TagMask);

	UFUNCTION(BlueprintCallable, Category="TempoZoneGraphUtils", meta=(WorldContext="WorldContextObject"))
	static FZoneGraphTagMask GenerateTagMaskFromTagNames(const UObject* WorldContextObject, const TArray<FName>& TagNames);

	UFUNCTION(BlueprintCallable, Category="TempoZoneGraphUtils", meta=(WorldContext="WorldContextObject"))
	static FTempoZoneGraphTagFilter GenerateTagFilter(const UObject* WorldContextObject, const TArray<FName>& AnyTags, const TArray<FName>& AllTags, const TArray<FName>& NotTags);

	// FMassTrafficLanePriorityFilters::LaneTagFilters, which holds the engine's tag filters and so
	// cannot be a Blueprint-visible property itself.
	UFUNCTION(BlueprintPure, Category="TempoZoneGraphUtils")
	static TArray<FTempoZoneGraphTagFilter> GetLaneTagFilters(const FMassTrafficLanePriorityFilters& LanePriorityFilters);

	UFUNCTION(BlueprintCallable, Category="TempoZoneGraphUtils")
	static void SetLaneTagFilters(UPARAM(ref) FMassTrafficLanePriorityFilters& LanePriorityFilters, const TArray<FTempoZoneGraphTagFilter>& LaneTagFilters);
};
