// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

template <typename TEnum>
FString GetEnumValueAsString(const TEnum Value, bool bQualified=false)
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
