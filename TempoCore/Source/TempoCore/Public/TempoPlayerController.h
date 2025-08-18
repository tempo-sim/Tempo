// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Components/SpotLightComponent.h"
#include "TempoPlayerController.generated.h"

class ASpectatorPawn;

// A struct to wrap the TArray, making it compatible with TMap UPROPERTYs.
USTRUCT()
struct FPawnGroup
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<APawn*> Pawns;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPawnListUpdated);

/**
 * A PlayerController that allows switching between different groups of Pawns.
 */
UCLASS(Blueprintable)
class TEMPOCORE_API ATempoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATempoPlayerController();

	virtual void OnPossess(APawn* InPawn) override;

	virtual void OnUnPossess() override;
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Tempo")
	void ToggleMouseCaptured();

	UFUNCTION(BlueprintCallable, Category = "Tempo")
	void SetMouseCaptured(bool bCaptured);
	
	UFUNCTION(BlueprintPure, Category = "Tempo")
	TArray<APawn*> GetAllPossessablePawns() const;

	/** This event is broadcast whenever the pawn groups are updated. */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPawnListUpdated OnPawnListUpdated;

	/**
	* @brief Caches the current AI controller of a pawn before the player possesses it.
	*/
	void CacheAIController(APawn* PawnToPossess);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tempo")
	bool bMouseCaptured = false;

	// A map to hold all pawns, dynamically grouped by their specific UClass.
    UPROPERTY()
    TMap<UClass*, FPawnGroup> PawnGroups;

    // An ordered list of the UClass keys from the PawnGroups map, for cycling.
    UPROPERTY()
    TArray<TSubclassOf<APawn>> PawnGroupClasses;
    
    // A map to store the current possession index for each pawn group.
    UPROPERTY()
    TMap<UClass*, int32> PawnGroupIndices;

    // The index of the currently active group within the PawnGroupClasses array.
    int32 ActiveGroupIndex;

    // A pointer to the currently active selection light so we can destroy it when we switch pawns.
    UPROPERTY()
    USpotLightComponent* ActiveSelectionLight;
    
    // A reference to the single SpectatorPawn in the level.
    UPROPERTY()
    ASpectatorPawn* LevelSpectatorPawn;

    // Delegate handles for cleaning up our event listeners.
    FDelegateHandle OnActorSpawnedDelegateHandle;
    FDelegateHandle OnActorDestroyedDelegateHandle;

    // Timer handle for the delayed update.
    FTimerHandle UpdateGroupsTimerHandle;

    // Variable to track if the mouse is captured for gameplay
    bool bIsMouseCaptured;
    
    /**
     * @brief Finds all actors of class Pawn, groups them by subclass, and sorts them.
     */
    void UpdatePawnGroups();

    /**
     * @brief Handler for the global OnActorSpawned event.
     * @param SpawnedActor The actor that was just spawned.
     */
    void OnActorSpawnedHandler(AActor* SpawnedActor);

    /**
     * @brief Handler for the global OnActorDestroyed event.
     * @param DestroyedActor The actor that was just destroyed.
     */
    void OnAnyActorDestroyedHandler(AActor* DestroyedActor);

    /**
     * @brief Switches the active pawn group to the next one discovered in the level.
     */
    void SwitchActiveGroup();

    /**
     * @brief Possesses the next Pawn in the currently active group's array.
     */
    void PossessNextPawn();

    /**
     * @brief Possesses the previous Pawn in the currently active group's array.
     */
    void PossessPreviousPawn();

    /**
     * @brief Performs a line trace from the cursor to select and possess a pawn.
     */
    void SelectAndPossessPawn();

	/**
	 * Stores a reference to the original AI controller that was possessing a pawn
	 * before the player took over. This acts as a "memory" to ensure the correct
	 * AI can be restored when the player is done.
	 */
	UPROPERTY()
	TMap<APawn*, AController*> AIControllerMap;

	/**
	 * @brief Forcibly possesses the level's SpectatorPawn.
	 */
	void EnterSpectatorMode();
};