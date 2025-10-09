// Copyright Tempo Simulation, LLC. All Rights Reserved
#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
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
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UTempoActionMapEntryWidget> EntryWidgetClass;

    /** The panel widget that will contain the list of bindings. Must be named "BindingList" in the UMG editor. */
    UPROPERTY(meta = (BindWidget))
    UPanelWidget* BindingList;

    virtual void NativeOnInitialized() override;

    /** Overridden to capture key presses when we are in a listening state. */
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    void RefreshBindingList();

    TArray<FActionBindingInfo> GetPlayerActionBindings();

    /** Rebinds a given action to a new key and saves the change. */
    void RebindActionKey(FName ActionName, FKey NewKey);

    /** Flag to determine if we should be capturing key presses. */
    bool bIsListeningForKey = false;

    FName ActionToRebind;

    /** A cached list of the entry widgets we've created. */
    UPROPERTY()
    TArray<TObjectPtr<UTempoActionMapEntryWidget>> EntryWidgets;
};
