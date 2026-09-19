// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MassTrafficTurnType.h"
#include "ZoneGraphTypes.h"

#include "TempoZoneGraphSettings.generated.h"

// Lanes with different tags that should nonetheless be connected through intersections.
USTRUCT()
struct FTempoZoneGraphCompatibleTags
{
	GENERATED_BODY()

	UPROPERTY(Category = CompatibleTags, EditAnywhere)
	FZoneGraphTag SourceTag;

	UPROPERTY(Category = CompatibleTags, EditAnywhere)
	FZoneGraphTag DestTag;

	UPROPERTY(Category = CompatibleTags, EditAnywhere)
	TSet<EMassTrafficTurnType> CompatibleForTurnTypes;
};

/**
 * Settings for how Tempo lays lanes through intersections when it builds the zone graph, beyond
 * the engine's own ZoneGraph build settings.
 */
UCLASS(Config=Plugins, DefaultConfig)
class TEMPOAGENTSEDITOR_API UTempoZoneGraphSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTempoZoneGraphSettings();

	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif

	// Tempo's old modifications to the engine's ZoneGraph kept these settings among its build
	// settings. Sets each of them that BuildSettingsText, the text of those build settings as a
	// project's config has it, has a value for. Returns how many that was.
	int32 ImportFromZoneGraphBuildSettings(const FString& BuildSettingsText);

	/** Tags which should be connected for certain turn types even when they are not the same. */
	UPROPERTY(EditAnywhere, Config, Category = "Lanes")
	TArray<FTempoZoneGraphCompatibleTags> CompatibleTags;

	/** Whether to remove overlapping lane connections. */
	UPROPERTY(EditAnywhere, Config, Category = "Lanes")
	bool bRemoveOverlap = true;

	/** Whether to remove lane connections when another already has the same destination. */
	UPROPERTY(EditAnywhere, Config, Category = "Lanes")
	bool bRemoveSameDestination = true;

	/** Whether to fill empty destination connections if possible. */
	UPROPERTY(EditAnywhere, Config, Category = "Lanes")
	bool bFillEmptyDestination = true;

	/** Whether to limit turning connections to only the left-most or right-most turning connections. */
	UPROPERTY(EditAnywhere, Config, Category = "Lanes")
	bool bSingleTurningConnectionPerTurnType = true;

	/** Scales the tangent lengths of the Bezier curves that turning connections follow. */
	UPROPERTY(EditAnywhere, Config, Category = "Lanes")
	float TempoBezierTangentLengthMultiplier = 1.0f;
};
