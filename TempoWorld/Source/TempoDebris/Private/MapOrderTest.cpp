// Copyright Vayu Robotics, Inc. All Rights Reserved

#include "MapOrderTest.h"

void AMapOrderTest::TestMap()
{
	FRandomStream RandomStream;
	TMap<FString, int32> Map;
	for (int32 I = 0; I < MapSize; ++I)
	{
		Map.Add(FString::FromInt(RandomStream.RandRange(0, MapSize)), I);
	}
	if (bSort)
	{
		Map.KeySort([](const FString& Left, const FString& Right) { return Left < Right; });
	}
	TArray<FString> FirstOrdering;
	for (const auto& Elem : Map)
	{
		if (bPrintMap)
		{
			UE_LOG(LogTemp, Warning, TEXT("Key: %s, Value: %d"), *Elem.Key, Elem.Value);
		}
		FirstOrdering.Add(Elem.Key);
	}
	for (int32 I = 0; I < Iterations; ++I)
	{
		int32 J = 0;
		for (const auto& Elem : Map)
		{
			if (Elem.Key != FirstOrdering[J])
			{
				UE_LOG(LogTemp, Error, TEXT("Map was not ordered the same way at the %d place in the %d iteration"), J, I);
			}
			J++;
		}
	}
}
