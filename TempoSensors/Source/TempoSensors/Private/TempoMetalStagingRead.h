// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_MAC

class FRHITexture;

// Copy ImageSize rows of PixelBytes-wide pixels out of a Metal staging texture into Dst (tightly
// packed), in NumBands parallel bands of rows, without mapping it. Only for a staging texture whose
// GPU writes are known to have completed (its fence has signaled): this neither waits on nor
// submits any GPU work. Returns false, having copied nothing, when the texture is not one this can
// read: the RHI is not Metal, or the texture's storage is not CPU-shared (Intel Macs, where Metal
// creates readback textures as managed and they have to be synchronized on the GPU first).
//
// RHIMapStagingSurface is the portable alternative, but Metal's waits for the whole GPU to go idle
// on every call, whether or not the fence has signaled. With captures pipelined that blocks the
// render thread behind every capture issued since, which in turn stalls the game thread.
bool TempoReadSignaledMetalStagingTexture(FRHITexture* Staging, uint8* Dst, const FIntPoint& ImageSize, int32 PixelBytes, int32 NumBands);

#endif
