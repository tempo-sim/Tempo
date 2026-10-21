// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMetalStagingRead.h"

#include "Async/ParallelFor.h"
#include "RHI.h"
#include "RHIResources.h"

// CarbonCore (pulled in by Foundation) declares its own FVector; rename it out of the way of
// Unreal's, as Mac/MacSystemIncludes.h does for the system headers it includes.
THIRD_PARTY_INCLUDES_START
#define FVector FVectorWorkaround
#import <Metal/Metal.h>
#undef FVector
THIRD_PARTY_INCLUDES_END

bool TempoReadSignaledMetalStagingTexture(FRHITexture* Staging, uint8* Dst, const FIntPoint& ImageSize, int32 PixelBytes, int32 NumBands)
{
	if (!Staging || RHIGetInterfaceType() != ERHIInterfaceType::Metal)
	{
		return false;
	}

	id<MTLTexture> Texture = (__bridge id<MTLTexture>)Staging->GetNativeResource();
	if (!Texture || Texture.storageMode != MTLStorageModeShared)
	{
		return false;
	}

	const int32 NumRows = ImageSize.Y;
	const int64 DstPitch = static_cast<int64>(ImageSize.X) * PixelBytes;
	ParallelFor(NumBands, [=](int32 Band)
	{
		const int32 BeginRow = static_cast<int32>(static_cast<int64>(NumRows) * Band / NumBands);
		const int32 EndRow = static_cast<int32>(static_cast<int64>(NumRows) * (Band + 1) / NumBands);
		[Texture getBytes:Dst + BeginRow * DstPitch
			  bytesPerRow:DstPitch
			   fromRegion:MTLRegionMake2D(0, BeginRow, ImageSize.X, EndRow - BeginRow)
			  mipmapLevel:0];
	});
	return true;
}
