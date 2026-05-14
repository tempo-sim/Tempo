// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCameraVideoEncoder.h"

#include "TempoSensors.h"

#include "AVDevice.h"
#include "AVResult.h"
#include "Video/VideoEncoder.h"
#include "Video/VideoPacket.h"
#include "Video/Encoders/Configs/VideoEncoderConfigH264.h"
#include "Video/CodecUtils/CodecUtilsH264.h"
#include "Video/Resources/VideoResourceRHI.h"

#include "Containers/Queue.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
	EH264Profile ToAVH264Profile(TempoSensors::H264Profile Profile)
	{
		switch (Profile)
		{
		case TempoSensors::H264Profile::H264_BASELINE:
			return EH264Profile::Baseline;
		case TempoSensors::H264Profile::H264_HIGH:
			return EH264Profile::High;
		case TempoSensors::H264Profile::H264_MAIN:
		default:
			return EH264Profile::Main;
		}
	}

	// Pick a sensible default bitrate when the request asks for 0 (auto). Scales with pixel count
	// at ~0.1 bits/pixel/frame at 30fps — a generic streaming-quality target.
	uint32 DefaultBitrateKbps(uint32 Width, uint32 Height, uint32 Framerate)
	{
		const uint64 Pixels = static_cast<uint64>(Width) * Height;
		const uint64 Bps = static_cast<uint64>(Pixels * Framerate) / 10; // ~0.1 bpp
		return static_cast<uint32>(FMath::Clamp<uint64>(Bps / 1000, 1000, 50000));
	}
}

struct FTempoCameraVideoEncoder::FImpl
{
	// Backing encoder. Holds onto the resource-typed encoder directly so the SendFrame call site
	// doesn't need a static_cast.
	TSharedPtr<TVideoEncoder<FVideoResourceRHI>> Encoder;

	// Hardware device this encoder runs against (cached from FAVDevice::GetHardwareDevice()).
	TSharedPtr<FAVDevice> Device;

	// Staging texture used to feed the encoder on Mac/Linux, where the camera's render target lacks
	// the platform-specific flag the encoder backend needs (CPUReadback for Metal/VideoToolbox,
	// External for Vulkan→CUDA interop). Allocated lazily, reused across frames when the descriptor
	// matches, copied into via CopyFrom each frame. Unused on Windows — we wrap the RT directly.
	TSharedPtr<FVideoResourceRHI> StagingResource;

	FTempoCameraVideoEncoder::FConfig AppliedConfig;
	bool bConfigured = false;

	// Frame counter used to drive periodic IDRs. Reset to 0 on every Configure() that opens a new
	// encoder, so the very first frame after open is always an IDR.
	uint64 FrameCount = 0;

	// MPSC packet queue: producer is the render thread, consumer is the game thread (SendMeasurements).
	TQueue<FTempoEncodedVideoPacket, EQueueMode::Mpsc> PacketQueue;

	bool IsOpen() const
	{
		return Encoder.IsValid() && Encoder->IsOpen();
	}

	void CloseEncoder()
	{
		if (Encoder.IsValid())
		{
			Encoder->Close();
			Encoder.Reset();
		}
		StagingResource.Reset();
		bConfigured = false;
		FrameCount = 0;
	}

	bool ReopenEncoder()
	{
		if (Encoder.IsValid())
		{
			Encoder->Close();
			Encoder.Reset();
		}

		if (AppliedConfig.Width == 0 || AppliedConfig.Height == 0)
		{
			return false;
		}

		if (AppliedConfig.Codec != TempoSensors::VideoCodec::H264)
		{
			UE_LOG(LogTempoSensors, Error, TEXT("FTempoCameraVideoEncoder: only H264 is supported."));
			return false;
		}

		if (!Device.IsValid())
		{
			Device = FAVDevice::GetHardwareDevice();
		}
		if (!Device.IsValid())
		{
			UE_LOG(LogTempoSensors, Error, TEXT("FTempoCameraVideoEncoder: no AVCodecs hardware device available."));
			return false;
		}

		const uint32 Bitrate = AppliedConfig.BitrateKbps > 0
			? AppliedConfig.BitrateKbps * 1000
			: DefaultBitrateKbps(AppliedConfig.Width, AppliedConfig.Height, AppliedConfig.TargetFramerate) * 1000;

		FVideoEncoderConfigH264 Config(EAVPreset::Default);
		Config.Width = AppliedConfig.Width;
		Config.Height = AppliedConfig.Height;
		Config.TargetFramerate = AppliedConfig.TargetFramerate;
		Config.TargetBitrate = static_cast<int32>(Bitrate);
		Config.MaxBitrate = static_cast<int32>(Bitrate * 2);
		Config.RateControlMode = ERateControlMode::CBR;
		Config.LatencyMode = EAVLatencyMode::UltraLowLatency;
		Config.KeyframeInterval = AppliedConfig.KeyframeInterval > 0 ? AppliedConfig.KeyframeInterval : 30;
		Config.Profile = ToAVH264Profile(AppliedConfig.H264Profile);
		// Auto-prepend SPS/PPS to every IDR. Lets a client that joins mid-stream decode from the
		// next keyframe without a separate parameter-set handshake.
		Config.RepeatSPSPPS = true;
		// EH264EntropyCodingMode::Auto maps to a null CFStringRef in the H264→VT config transform,
		// which then crashes inside VTCodecs::SetVTSessionProperty (no null-check before
		// CFStringToString). Pin to CABAC to avoid the crash. Matches what PixelStreaming does.
		Config.EntropyCodingMode = EH264EntropyCodingMode::CABAC;
		Config.AdaptiveTransformMode = EH264AdaptiveTransformMode::Enable;

		Encoder = FVideoEncoder::Create<FVideoResourceRHI>(Device.ToSharedRef(), Config);
		if (!Encoder.IsValid())
		{
			UE_LOG(LogTempoSensors, Error, TEXT("FTempoCameraVideoEncoder: failed to create H264 encoder. Is a vendor codec plugin (NVCodecs/VTCodecs/AMF/WMF) enabled and supported on this device?"));
			return false;
		}

		FrameCount = 0;
		return true;
	}
};

FTempoCameraVideoEncoder::FTempoCameraVideoEncoder()
	: Impl(MakeUnique<FImpl>())
{
}

FTempoCameraVideoEncoder::~FTempoCameraVideoEncoder()
{
	if (Impl.IsValid())
	{
		Impl->CloseEncoder();
	}
}

bool FTempoCameraVideoEncoder::IsOpen() const
{
	return Impl.IsValid() && Impl->IsOpen();
}

void FTempoCameraVideoEncoder::Close()
{
	if (Impl.IsValid())
	{
		Impl->CloseEncoder();
	}
}

bool FTempoCameraVideoEncoder::Configure(const FConfig& NewConfig)
{
	if (!Impl.IsValid())
	{
		return false;
	}

	const FConfig& Cur = Impl->AppliedConfig;
	const bool bSameTopology =
		Impl->bConfigured
		&& Impl->IsOpen()
		&& Cur.Width == NewConfig.Width
		&& Cur.Height == NewConfig.Height
		&& Cur.Codec == NewConfig.Codec
		&& Cur.H264Profile == NewConfig.H264Profile
		&& Cur.KeyframeInterval == NewConfig.KeyframeInterval
		&& Cur.BitrateKbps == NewConfig.BitrateKbps
		&& Cur.TargetFramerate == NewConfig.TargetFramerate;

	if (bSameTopology)
	{
		return true;
	}

	Impl->AppliedConfig = NewConfig;
	Impl->bConfigured = true;
	return Impl->ReopenEncoder();
}

void FTempoCameraVideoEncoder::EncodeRenderTarget_RenderThread(UTextureRenderTarget2D* RenderTarget, double CaptureTime, int32 SequenceId)
{
	check(IsInRenderingThread());

	if (!Impl.IsValid() || !Impl->IsOpen() || !RenderTarget)
	{
		return;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GetRenderTargetResource();
	if (!RTResource)
	{
		return;
	}

	FTextureRHIRef Texture = RTResource->GetRenderTargetTexture();
	if (!Texture.IsValid())
	{
		return;
	}

	const uint32 KFI = Impl->AppliedConfig.KeyframeInterval > 0 ? Impl->AppliedConfig.KeyframeInterval : 30;
	const bool bForceKeyframe = (Impl->FrameCount % KFI) == 0;

	const TSharedRef<FAVDevice> EncoderDevice = Impl->Encoder->GetDevice().ToSharedRef();

	// On Mac/Linux the camera's RT lacks the platform flag the encoder backend needs, so we own a
	// staging resource and CopyFrom into it before submitting:
	//   - Mac (VideoToolbox via Metal): CPUReadback. The RHI->Metal resource transform asserts on it.
	//   - Linux (NVENC via Vulkan->CUDA interop): External. NVCodecs imports the Vulkan image via
	//     VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, then cuExternalMemoryGetMappedMipmappedArray
	//     it for the encoder. The External flag is what tells the RHI to allocate the image's
	//     memory with VK_EXPORT_MEMORY_ALLOCATE_INFO + use VK_IMAGE_TILING_OPTIMAL. Without it,
	//     CUDA reports "Failed to bind mipmappedArray".
	// On Windows the camera RT already carries Shared (UTempoCamera sets bGPUSharedFlag = true on
	// SharedFinalTextureTarget), so we can wrap and submit it directly. Allocating a staging RT
	// through FVideoResourceRHI::Create here produced a placed pool-allocator resource that
	// NVENC's D3D12 path rejected with INVALID_PARAM (NVENC 8) inside nvEncRegisterResource.
	TSharedPtr<FVideoResourceRHI> InputResource;
#if PLATFORM_WINDOWS
	{
		FVideoResourceRHI::FRawData Raw;
		Raw.Texture = Texture;
		Raw.FenceValue = 0;
		InputResource = MakeShareable(new FVideoResourceRHI(EncoderDevice, Raw));
	}
#else
	constexpr ETextureCreateFlags StagingFlags =
#if PLATFORM_APPLE
		ETextureCreateFlags::CPUReadback;
#elif PLATFORM_LINUX
		ETextureCreateFlags::External;
#else
		ETextureCreateFlags::None;
#endif

	const FVideoDescriptor Desc = FVideoResourceRHI::GetDescriptorFrom(EncoderDevice, Texture);
	if (!Impl->StagingResource.IsValid()
		|| Impl->StagingResource->GetDescriptor() != Desc)
	{
		Impl->StagingResource = FVideoResourceRHI::Create(
			EncoderDevice,
			Desc,
			StagingFlags);
		if (!Impl->StagingResource.IsValid())
		{
			UE_LOG(LogTempoSensors, Warning, TEXT("FTempoCameraVideoEncoder: failed to allocate staging FVideoResourceRHI."));
			return;
		}
	}
	Impl->StagingResource->CopyFrom(Texture);
	InputResource = Impl->StagingResource;
#endif

	const uint32 Timestamp = static_cast<uint32>(Impl->FrameCount);
	const FAVResult SendResult = Impl->Encoder->SendFrame(InputResource, Timestamp, bForceKeyframe);
	if (SendResult.IsNotSuccess())
	{
		UE_LOG(LogTempoSensors, Warning, TEXT("FTempoCameraVideoEncoder::SendFrame failed: %s"), *SendResult.Message);
		return;
	}

	// Drain whatever's ready right now. NVENC/VT are typically one-in-one-out at low latency, so
	// this usually pulls exactly one packet per frame. When the codec splits one access unit into
	// multiple FVideoPackets (e.g. SPS/PPS + slice NALs on an IDR), we concatenate the Annex-B
	// bytes into a single FTempoEncodedVideoPacket so each frame maps 1:1 to a wire VideoFrame.
	FTempoEncodedVideoPacket Aggregate;
	bool bGotAny = false;
	FVideoPacket RawPacket;
	while (Impl->Encoder->ReceivePacket(RawPacket).IsSuccess())
	{
		bGotAny = true;
		Aggregate.bIsKeyframe |= !!RawPacket.bIsKeyframe;
		if (RawPacket.DataSize > 0 && RawPacket.DataPtr.IsValid())
		{
			Aggregate.Data.Append(RawPacket.DataPtr.Get(), RawPacket.DataSize);
		}
	}

	if (bGotAny)
	{
		Aggregate.Width = Impl->AppliedConfig.Width;
		Aggregate.Height = Impl->AppliedConfig.Height;
		Aggregate.CaptureTime = CaptureTime;
		Aggregate.SequenceId = SequenceId;
		Impl->PacketQueue.Enqueue(MoveTemp(Aggregate));
	}

	++Impl->FrameCount;
}

void FTempoCameraVideoEncoder::DrainPackets(TArray<FTempoEncodedVideoPacket>& OutPackets)
{
	if (!Impl.IsValid())
	{
		return;
	}
	FTempoEncodedVideoPacket Packet;
	while (Impl->PacketQueue.Dequeue(Packet))
	{
		OutPackets.Add(MoveTemp(Packet));
	}
}
