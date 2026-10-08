// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoZoneGraphSettings.h"

#include "TempoAgentsEditor.h"

#include "Misc/ConfigCacheIni.h"
#include "ZoneGraphSettings.h"

namespace
{
	// The text of Key's value among the comma-separated Key=Value pairs directly inside StructText's
	// outermost parentheses, or an empty string if it has none.
	FString FindValueInStructText(const FString& StructText, const FString& Key)
	{
		int32 Depth = 0;
		bool bInQuotes = false;
		int32 NumPrecedingBackslashes = 0;
		int32 PairStart = INDEX_NONE;
		for (int32 Index = 0; Index <= StructText.Len(); ++Index)
		{
			const TCHAR Char = Index < StructText.Len() ? StructText[Index] : TEXT(')');
			// A quote is escaped by an odd run of backslashes before it: "ends in a backslash\\" does not.
			if (Char == TEXT('"') && NumPrecedingBackslashes % 2 == 0)
			{
				bInQuotes = !bInQuotes;
			}
			NumPrecedingBackslashes = Char == TEXT('\\') ? NumPrecedingBackslashes + 1 : 0;
			if (bInQuotes)
			{
				continue;
			}

			const bool bEndsPair = Depth == 1 && (Char == TEXT(',') || Char == TEXT(')'));
			if (bEndsPair && PairStart != INDEX_NONE)
			{
				const FString Pair = StructText.Mid(PairStart, Index - PairStart).TrimStartAndEnd();
				if (Pair.StartsWith(Key + TEXT("="), ESearchCase::CaseSensitive))
				{
					return Pair.RightChop(Key.Len() + 1);
				}
			}

			if (Char == TEXT('('))
			{
				++Depth;
			}
			else if (Char == TEXT(')'))
			{
				--Depth;
			}
			if (Depth == 1 && (Char == TEXT('(') || Char == TEXT(',')))
			{
				PairStart = Index + 1;
			}
		}
		return FString();
	}
}

UTempoZoneGraphSettings::UTempoZoneGraphSettings()
{
	CategoryName = TEXT("Tempo");
}

#if WITH_EDITOR
FText UTempoZoneGraphSettings::GetSectionText() const
{
	return FText::FromString(FString(TEXT("Zone Graph Build")));
}
#endif

void UTempoZoneGraphSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// A project that set these when they were among the ZoneGraph settings, and has not set them here
	// since, still means what it set there.
	if (!HasAnyFlags(RF_ClassDefaultObject) || GConfig == nullptr || GConfig->DoesSectionExist(*GetClass()->GetPathName(), GetClass()->GetConfigName()))
	{
		return;
	}

	FString BuildSettingsText;
	const UClass* ZoneGraphSettingsClass = UZoneGraphSettings::StaticClass();
	if (GConfig->GetString(*ZoneGraphSettingsClass->GetPathName(), TEXT("BuildSettings"), BuildSettingsText, ZoneGraphSettingsClass->GetConfigName())
		&& ImportFromZoneGraphBuildSettings(BuildSettingsText) > 0)
	{
		UE_LOG(LogTempoAgentsEditor, Display, TEXT("Using the zone graph build settings this project set among the ZoneGraph settings' BuildSettings. "
			"Set them under Project Settings > Tempo > Zone Graph Build to keep them."));
	}
}

int32 UTempoZoneGraphSettings::ImportFromZoneGraphBuildSettings(const FString& BuildSettingsText)
{
	int32 NumImported = 0;
	for (TFieldIterator<FProperty> PropertyIt(GetClass(), EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
	{
		const FString ValueText = FindValueInStructText(BuildSettingsText, PropertyIt->GetName());
		if (!ValueText.IsEmpty() && PropertyIt->ImportText_InContainer(*ValueText, this, this, PPF_None) != nullptr)
		{
			++NumImported;
		}
	}
	return NumImported;
}
