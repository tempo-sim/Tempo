#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "TempoActionMapWidget.generated.h"

// Forward declarations
class UPanelWidget;
class UTempoActionMapEntryWidget;

USTRUCT(BlueprintType)
struct FActionBindingInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FString ActionDisplayName;

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FName ActionName;

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FString KeyName;

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FKey Key;
};

UCLASS()
class TEMPOCORE_API UTempoActionMapWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void StartListeningForNewKey(FName ActionNameToRebind);

protected:
    /** The Blueprint class to use for creating entry widgets. This is exposed to the editor. */
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UTempoActionMapEntryWidget> EntryWidgetClass;

    /** The panel widget that will contain the list of bindings. Must be named "BindingList" in the UMG editor. */
    UPROPERTY(meta = (BindWidget))
    UPanelWidget* BindingList;

    /** Overridden to populate the list when the widget is created. */
    virtual void NativeOnInitialized() override;

    /** Overridden to capture key presses when we are in a listening state. */
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    /** Clears and rebuilds the list of binding entries. */
    void RefreshBindingList();

    /** Gathers all relevant action bindings from the player's input component. */
    TArray<FActionBindingInfo> GetPlayerActionBindings();

    /** Rebinds a given action to a new key and saves the change. */
    void RebindActionKey(FName ActionName, FKey NewKey);

    /** Flag to determine if we should be capturing key presses. */
    bool bIsListeningForKey = false;

    /** Stores the name of the action we are currently trying to rebind. */
    FName ActionToRebind;

    /** A cached list of the entry widgets we've created. */
    UPROPERTY()
    TArray<TObjectPtr<UTempoActionMapEntryWidget>> EntryWidgets;
};
