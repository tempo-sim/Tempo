// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoCoreUtils.generated.h"

UCLASS()
class TEMPOCORESHARED_API UTempoCoreUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	template <typename TEnum>
	static FString GetEnumValueAsString(const TEnum Value, bool bQualified=false)
	{
		FString ValueString = UEnum::GetValueAsString(Value);
		if (!bQualified)
		{
			if (int32 LastColonIdx; ValueString.FindLastChar(':', LastColonIdx))
			{
				ValueString.RightChopInline(LastColonIdx + 1);
			}
		}
		return ValueString;
	}

	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils",  meta=(WorldContext="WorldContextObject", DeterminesOutputType="Interface"))
	static UWorldSubsystem* GetSubsystemImplementingInterface(const UObject* WorldContextObject, TSubclassOf<UInterface> Interface);

	// Is the world owning this object a PIE or Game world?
	// Note that UWorld::GetWorld() considers GamePreview and GameRPC worlds to be Game worlds, which we do not.
	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils",  meta=(WorldContext="WorldContextObject"))
	static bool IsGameWorld(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils")
	static FBox GetActorLocalBounds(const AActor* Actor);
};
