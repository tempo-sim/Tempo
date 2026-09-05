// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "CoreMinimal.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "EditorUtilityActor.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficEditor.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficEditorBaseActor.generated.h"

#define USE_CUSTOM_EVENT false


UCLASS(BlueprintType)
class MASSTRAFFICEDITOR_API AMassTrafficEditorBaseActor : public AEditorUtilityActor
{
	GENERATED_BODY()
	
public:

	AMassTrafficEditorBaseActor();

	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass Traffic|Mass Traffic Editor|Tick")
	bool CanTickInEditor = true;

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Editor|Mass Traffic Builder|Editor")
    void RefreshEditor();
};
