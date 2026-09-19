// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "ZoneGraphTypes.h"

#include "TempoLaneProfileStore.generated.h"

/**
 * Holds the lane profiles Tempo generates for a level's roads, as opposed to the ones configured in
 * the project's ZoneGraph settings.
 *
 * ZoneGraph resolves a zone shape's lane profile from those settings and nowhere else, so while
 * this Actor is registered in an editor world its profiles are added to the settings in memory.
 * They are held back whenever the settings change, so that saving the settings never writes them
 * to the project's config.
 */
UCLASS(NotBlueprintable, NotPlaceable)
class TEMPOAGENTS_API ATempoLaneProfileStore : public AInfo
{
	GENERATED_BODY()

public:
	ATempoLaneProfileStore();

	const TArray<FZoneLaneProfile>& GetLaneProfiles() const { return LaneProfiles; }

#if WITH_EDITOR
	// The store in World's persistent level, optionally spawning it if there is none.
	static ATempoLaneProfileStore* Get(UWorld& World, bool bSpawnIfMissing);

	// The stored profile with the same lanes as LaneProfile, which is stored first if there is none.
	FZoneLaneProfile FindOrAddLaneProfile(const FZoneLaneProfile& LaneProfile);

	// Stores those of LaneProfiles whose IDs are not stored yet, as they are. Returns how many that was.
	int32 AddLaneProfiles(TConstArrayView<FZoneLaneProfile> InLaneProfiles);

	void ClearLaneProfiles();

	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void PostEditUndo() override;
#endif

protected:
	UPROPERTY(VisibleAnywhere, Category = "Tempo")
	TArray<FZoneLaneProfile> LaneProfiles;

#if WITH_EDITOR
	// Make the settings hold exactly this store's profiles, or none of them.
	void SyncSettings(bool bShouldBeInSettings);
#endif

#if WITH_EDITORONLY_DATA
	// The IDs of the profiles this store has added to the settings.
	TArray<FGuid> LaneProfileIDsInSettings;
	bool bIsSyncedToSettings = false;
#endif
};
