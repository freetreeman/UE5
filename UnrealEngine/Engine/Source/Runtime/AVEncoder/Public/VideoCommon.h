// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

//
// Windows only include
//
#if (PLATFORM_WINDOWS || PLATFORM_HOLOLENS)

#pragma warning(push)
#pragma warning(disable: 4005)

THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include <d3d11.h>
#include <mftransform.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <codecapi.h>
#include <shlwapi.h>
#include <mfreadwrite.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_END

struct ID3D11DeviceChild;

#endif // PLATFORM_WINDOWS

#define WMFMEDIA_SUPPORTED_PLATFORM (PLATFORM_WINDOWS && !UE_SERVER)

namespace AVEncoder
{
	const int64 TimeStampNone = 0x7fffffffll;

	enum class EVideoFrameFormat
	{
		Undefined,				// (not-yet) defined format
		YUV420P,				// Planar YUV420 format in CPU memory
		D3D11_R8G8B8A8_UNORM,	//
		D3D12_R8G8B8A8_UNORM,	//
		CUDA_R8G8B8A8_UNORM,
		VULKAN_R8G8B8A8_UNORM,
	};

	enum class EH264Profile
	{
		UNKNOWN,
		CONSTRAINED_BASELINE,
		BASELINE,
		MAIN,
		CONSTRAINED_HIGH,
		HIGH,
	};

	inline FString ToString(EVideoFrameFormat Format)
	{
		switch (Format)
		{
		case EVideoFrameFormat::YUV420P:
			return FString("EVideoFrameFormat::YUV420P");
		case EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
			return FString("EVideoFrameFormat::D3D11_R8G8B8A8_UNORM");
		case EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
			return FString("EVideoFrameFormat::D3D12_R8G8B8A8_UNORM");
		case EVideoFrameFormat::CUDA_R8G8B8A8_UNORM:
			return FString("EVideoFrameFormat::CUDA_R8G8B8A8_UNORM");
		case EVideoFrameFormat::VULKAN_R8G8B8A8_UNORM:
			return FString("EVideoFrameFormat::VULKAN_R8G8B8A8_UNORM");
		case EVideoFrameFormat::Undefined:
		default:
			return FString("EVideoFrameFormat::Undefined");
		}
	}

	enum class ECodecType
	{
		Undefined,
		H264,
		MPEG4,
		VP8,
	};

	// TODO: make enums
	const uint32 H264Profile_ConstrainedBaseline = 1 << 0;
	const uint32 H264Profile_Baseline = 1 << 1;
	const uint32 H264Profile_Main = 1 << 2;
	const uint32 H264Profile_ConstrainedHigh = 1 << 3;
	const uint32 H264Profile_High = 1 << 4;

	class FCodecPacket
	{
	public:
		// clone packet if a longer term copy is needed
		virtual const FCodecPacket* Clone() const = 0;
		// release a cloned copy
		virtual void ReleaseClone() const = 0;

		int64			PTS = TimeStampNone;	// presentation timestamp (within TimeBase)
		int64			DTS = TimeStampNone;	// decode timestamp (within TimeBase)
		const uint8*	Data = nullptr;			// pointer to encoded data
		uint32			DataSize = 0;			// number of bytes of encoded data
		bool			IsKeyFrame = false;		// whether or not packet represents a key frame
		uint32			VideoQP = 0;

		/**
		 * Encoding/Decoding latency
		 */
		struct
		{
			FTimespan StartTs;
			FTimespan FinishTs;
		} Timings;

		uint32 Framerate;

	protected:
		FCodecPacket() = default;
		virtual ~FCodecPacket() = default;
		FCodecPacket(const FCodecPacket&) = delete;
		FCodecPacket& operator=(const FCodecPacket&) = delete;
	};

	struct FVideoEncoderInfo
	{
		uint32						ID = 0;
		ECodecType					CodecType = ECodecType::Undefined;
		uint32						MaxWidth = 0;
		uint32						MaxHeight = 0;
		TArray<EVideoFrameFormat>	SupportedInputFormats;
		struct
		{
			uint32					SupportedProfiles = 0;
			uint32					MinLevel = 0;
			uint32					MaxLevel = 0;
		}							H264;
	};


	struct FVideoDecoderInfo
	{
		uint32						ID = 0;
		ECodecType					CodecType = ECodecType::Undefined;
		uint32						MaxWidth = 0;
		uint32						MaxHeight = 0;
	};

#if PLATFORM_WINDOWS
	void DebugSetD3D11ObjectName(ID3D11DeviceChild* InD3DObject, const char* InName);
#endif
} /* namespace AVEncoder */
