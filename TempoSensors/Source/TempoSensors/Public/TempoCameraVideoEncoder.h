// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSensors/Camera.pb.h"

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

class UTextureRenderTarget2D;

// One encoded H.264 frame, with the metadata needed to fill out a TempoSensors::VideoFrame proto.
struct FTempoEncodedVideoPacket
{
	TArray<uint8> Data;
	uint32 Width = 0;
	uint32 Height = 0;
	bool bIsKeyframe = false;
	double CaptureTime = 0.0;
	int32 SequenceId = 0;
};

// Wraps an AVCodecs H.264 encoder running against an Unreal RHI texture target. Lifecycle:
//   1. Configure(...) opens (or reconfigures) the encoder for a given size/codec/bitrate/KFI/profile.
//   2. EncodeRenderTarget_RenderThread(...) is called from the camera's render-thread hook with the
//      camera's final texture target. Forces an IDR every KeyframeInterval frames.
//   3. DrainPackets(...) is called from the game thread to pull encoded packets out.
//   4. Close() tears the encoder down (e.g. when no streamers are subscribed for a while).
//
// Lossy and not bit-deterministic across runs — the shared instance broadcasts the same encoded
// bytes to every pending VideoRequest, so late-joining clients have to wait for the next IDR.
class TEMPOSENSORS_API FTempoCameraVideoEncoder
{
public:
	FTempoCameraVideoEncoder();
	~FTempoCameraVideoEncoder();

	// Resolution + parameter set. Reopening with different values closes the old encoder cleanly
	// and forces an IDR on the next encoded frame.
	struct FConfig
	{
		uint32 Width = 0;
		uint32 Height = 0;
		uint32 TargetFramerate = 30;
		uint32 BitrateKbps = 8000;
		uint32 KeyframeInterval = 30;
		TempoSensors::VideoCodec Codec = TempoSensors::VideoCodec::H264;
		TempoSensors::H264Profile H264Profile = TempoSensors::H264Profile::H264_MAIN;
	};

	// Open or reconfigure the encoder. Cheap when NewConfig matches the currently-applied config.
	// Returns false if no compatible encoder backend is available on this device.
	bool Configure(const FConfig& NewConfig);

	bool IsOpen() const;

	void Close();

	// Render-thread entry point. Wraps the texture target's RHI texture in an FVideoResourceRHI
	// and submits it to the encoder. Forces an IDR if FrameCount is divisible by KeyframeInterval.
	// CaptureTime/SequenceId are stamped onto each packet drained for this frame.
	void EncodeRenderTarget_RenderThread(UTextureRenderTarget2D* RenderTarget, double CaptureTime, int32 SequenceId);

	// Game-thread drain. Moves all completed packets out of the internal queue.
	void DrainPackets(TArray<FTempoEncodedVideoPacket>& OutPackets);

private:
	// pImpl. Keeps AVCodecs types (FVideoEncoder, FVideoResourceRHI, FAVDevice, packet queue,
	// applied config) out of the public header so consumers don't need AVCodecs on their include
	// path.
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
