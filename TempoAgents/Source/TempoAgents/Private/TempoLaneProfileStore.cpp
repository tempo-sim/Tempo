// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLaneProfileStore.h"

#include "TempoAgents.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CoreDelegates.h"
#include "ZoneGraphSettings.h"

namespace
{
	// How many registered stores hold each lane profile (by ID) that is, or should be, in the settings.
	TMap<FGuid, int32> SettingsLaneProfileRefCounts;

	// The profiles taken out of the settings while they are saved, to be put back at the end of the frame.
	TArray<FZoneLaneProfile> HeldBackLaneProfiles;

	FDelegateHandle RestoreHeldBackLaneProfilesHandle;

	// The settings keep their lane profiles protected, and offer no way to add one.
	TArray<FZoneLaneProfile>* GetSettingsLaneProfiles()
	{
		const FArrayProperty* LaneProfilesProperty = FindFProperty<FArrayProperty>(UZoneGraphSettings::StaticClass(), TEXT("LaneProfiles"));
		const FStructProperty* LaneProfileProperty = LaneProfilesProperty ? CastField<FStructProperty>(LaneProfilesProperty->Inner) : nullptr;
		if (!ensureMsgf(LaneProfileProperty && LaneProfileProperty->Struct == FZoneLaneProfile::StaticStruct(),
			TEXT("UZoneGraphSettings no longer has an array of FZoneLaneProfile named LaneProfiles. Tempo's generated lane profiles will not resolve.")))
		{
			return nullptr;
		}
		return LaneProfilesProperty->ContainerPtrToValuePtr<TArray<FZoneLaneProfile>>(GetMutableDefault<UZoneGraphSettings>());
	}

	void RestoreHeldBackLaneProfiles()
	{
		FCoreDelegates::OnEndFrame.Remove(RestoreHeldBackLaneProfilesHandle);
		RestoreHeldBackLaneProfilesHandle.Reset();

		if (TArray<FZoneLaneProfile>* SettingsLaneProfiles = GetSettingsLaneProfiles())
		{
			for (FZoneLaneProfile& LaneProfile : HeldBackLaneProfiles)
			{
				// A store may have unregistered while its profiles were held back.
				if (SettingsLaneProfileRefCounts.Contains(LaneProfile.ID))
				{
					SettingsLaneProfiles->Add(MoveTemp(LaneProfile));
				}
			}
		}
		HeldBackLaneProfiles.Reset();
	}

	// The settings editor saves the settings to the project's config right after they change.
	void HoldBackLaneProfilesWhileSettingsAreSaved(UObject*, FPropertyChangedEvent&)
	{
		TArray<FZoneLaneProfile>* SettingsLaneProfiles = GetSettingsLaneProfiles();
		if (SettingsLaneProfiles == nullptr || RestoreHeldBackLaneProfilesHandle.IsValid())
		{
			return;
		}

		for (int32 Index = SettingsLaneProfiles->Num() - 1; Index >= 0; --Index)
		{
			if (SettingsLaneProfileRefCounts.Contains((*SettingsLaneProfiles)[Index].ID))
			{
				HeldBackLaneProfiles.Add(MoveTemp((*SettingsLaneProfiles)[Index]));
				SettingsLaneProfiles->RemoveAt(Index);
			}
		}
		RestoreHeldBackLaneProfilesHandle = FCoreDelegates::OnEndFrame.AddStatic(&RestoreHeldBackLaneProfiles);
	}

	void AddLaneProfileToSettings(const FZoneLaneProfile& LaneProfile)
	{
		static bool bListeningForSettingsChanges = false;
		if (!bListeningForSettingsChanges)
		{
			GetMutableDefault<UZoneGraphSettings>()->OnSettingChanged().AddStatic(&HoldBackLaneProfilesWhileSettingsAreSaved);
			bListeningForSettingsChanges = true;
		}

		int32& RefCount = SettingsLaneProfileRefCounts.FindOrAdd(LaneProfile.ID, 0);
		if (++RefCount > 1)
		{
			return;
		}

		if (RestoreHeldBackLaneProfilesHandle.IsValid())
		{
			HeldBackLaneProfiles.Add(LaneProfile);
		}
		else if (TArray<FZoneLaneProfile>* SettingsLaneProfiles = GetSettingsLaneProfiles())
		{
			SettingsLaneProfiles->Add(LaneProfile);
		}
	}

	void RemoveLaneProfileFromSettings(const FGuid& LaneProfileID)
	{
		int32* RefCount = SettingsLaneProfileRefCounts.Find(LaneProfileID);
		if (RefCount == nullptr || --(*RefCount) > 0)
		{
			return;
		}
		SettingsLaneProfileRefCounts.Remove(LaneProfileID);

		const auto HasID = [&LaneProfileID](const FZoneLaneProfile& LaneProfile) { return LaneProfile.ID == LaneProfileID; };
		HeldBackLaneProfiles.RemoveAll(HasID);
		if (TArray<FZoneLaneProfile>* SettingsLaneProfiles = GetSettingsLaneProfiles())
		{
			SettingsLaneProfiles->RemoveAll(HasID);
		}
	}
}
#endif // WITH_EDITOR

ATempoLaneProfileStore::ATempoLaneProfileStore()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;
}

#if WITH_EDITOR
ATempoLaneProfileStore* ATempoLaneProfileStore::Get(UWorld& World, bool bSpawnIfMissing)
{
	for (TActorIterator<ATempoLaneProfileStore> StoreIt(&World); StoreIt; ++StoreIt)
	{
		if (StoreIt->GetLevel() == World.PersistentLevel)
		{
			return *StoreIt;
		}
	}

	if (!bSpawnIfMissing)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.OverrideLevel = World.PersistentLevel;
	return World.SpawnActor<ATempoLaneProfileStore>(SpawnParameters);
}

FZoneLaneProfile ATempoLaneProfileStore::FindOrAddLaneProfile(const FZoneLaneProfile& LaneProfile)
{
	if (const FZoneLaneProfile* StoredLaneProfile = LaneProfiles.FindByPredicate([&LaneProfile](const FZoneLaneProfile& Other) { return Other.Lanes == LaneProfile.Lanes; }))
	{
		return *StoredLaneProfile;
	}

	Modify();
	LaneProfiles.Add(LaneProfile);
	SyncSettings(bIsSyncedToSettings);
	return LaneProfile;
}

int32 ATempoLaneProfileStore::AddLaneProfiles(TConstArrayView<FZoneLaneProfile> InLaneProfiles)
{
	int32 NumAdded = 0;
	for (const FZoneLaneProfile& LaneProfile : InLaneProfiles)
	{
		if (LaneProfiles.ContainsByPredicate([&LaneProfile](const FZoneLaneProfile& Other) { return Other.ID == LaneProfile.ID; }))
		{
			continue;
		}
		if (NumAdded++ == 0)
		{
			Modify();
		}
		LaneProfiles.Add(LaneProfile);
	}

	if (NumAdded > 0)
	{
		SyncSettings(bIsSyncedToSettings);
	}
	return NumAdded;
}

void ATempoLaneProfileStore::ClearLaneProfiles()
{
	if (LaneProfiles.IsEmpty())
	{
		return;
	}

	Modify();
	LaneProfiles.Reset();
	SyncSettings(bIsSyncedToSettings);
}

void ATempoLaneProfileStore::SyncSettings(bool bShouldBeInSettings)
{
	for (const FGuid& LaneProfileID : LaneProfileIDsInSettings)
	{
		RemoveLaneProfileFromSettings(LaneProfileID);
	}
	LaneProfileIDsInSettings.Reset();

	if (bShouldBeInSettings)
	{
		for (const FZoneLaneProfile& LaneProfile : LaneProfiles)
		{
			AddLaneProfileToSettings(LaneProfile);
			LaneProfileIDsInSettings.Add(LaneProfile.ID);
		}
	}
	bIsSyncedToSettings = bShouldBeInSettings;
}

void ATempoLaneProfileStore::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	const UWorld* World = GetWorld();
	if (World != nullptr && !World->IsGameWorld())
	{
		SyncSettings(true);
	}
}

void ATempoLaneProfileStore::PostUnregisterAllComponents()
{
	SyncSettings(false);

	Super::PostUnregisterAllComponents();
}

void ATempoLaneProfileStore::PostEditUndo()
{
	Super::PostEditUndo();

	SyncSettings(bIsSyncedToSettings);
}
#endif // WITH_EDITOR
