// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Player/AdaptiveStreamingPlayerInternal.h"
#include "Player/AdaptivePlayerOptionKeynames.h"
#include "Utilities/Utilities.h"
#include "ParameterDictionary.h"


namespace Electra
{


bool FAdaptiveStreamingPlayer::FindMatchingStreamInfo(FStreamCodecInformation& OutStreamInfo, const FTimeValue& AtTime, int32 MaxWidth, int32 MaxHeight)
{
	TArray<FStreamCodecInformation> VideoCodecInfos;
	TSharedPtrTS<ITimelineMediaAsset> Asset;

	if (ActivePeriods.Num() == 0)
	{
		return false;
	}

	// Locate the period for the specified time.
	for(int32 i=0; i<ActivePeriods.Num(); ++i)
	{
		if (AtTime >= ActivePeriods[i].TimeRange.Start && AtTime < (ActivePeriods[i].TimeRange.End.IsValid() ? ActivePeriods[i].TimeRange.End : FTimeValue::GetPositiveInfinity()))
		{
			Asset = ActivePeriods[i].Period;
		}
	}
	if (Asset.IsValid())
	{
		if (Asset->GetNumberOfAdaptationSets(EStreamType::Video) > 0)
		{
			// What if this is more than one?
			TSharedPtrTS<IPlaybackAssetAdaptationSet> VideoSet = Asset->GetAdaptationSetByTypeAndIndex(EStreamType::Video, 0);
			check(VideoSet.IsValid());
			if (VideoSet.IsValid())
			{
				for(int32 i = 0, iMax = VideoSet->GetNumberOfRepresentations(); i < iMax; ++i)
				{
					VideoCodecInfos.Push(VideoSet->GetRepresentationByIndex(i)->GetCodecInformation());
				}
				check(VideoCodecInfos.Num());
				if (VideoCodecInfos.Num())
				{
					if (MaxWidth == 0 && MaxHeight == 0)
					{
						FStreamCodecInformation Best = VideoCodecInfos[0];
						for(int32 i=1; i<VideoCodecInfos.Num(); ++i)
						{
							const FStreamCodecInformation::FResolution& Res = VideoCodecInfos[i].GetResolution();
							if (Res.Width > Best.GetResolution().Width)
							{
								Best.SetResolution(FStreamCodecInformation::FResolution(Res.Width, Best.GetResolution().Height));
							}
							if (Res.Height > Best.GetResolution().Height)
							{
								Best.SetResolution(FStreamCodecInformation::FResolution(Best.GetResolution().Width, Res.Height));
							}
							// Note: the final RFC 6381 codec string will be bogus since we do not re-create it here.
							if (VideoCodecInfos[i].GetProfile() > Best.GetProfile())
							{
								Best.SetProfile(VideoCodecInfos[i].GetProfile());
							}
							if (VideoCodecInfos[i].GetProfileLevel() > Best.GetProfileLevel())
							{
								Best.SetProfileLevel(VideoCodecInfos[i].GetProfileLevel());
							}
							if (VideoCodecInfos[i].GetExtras().GetValue("b_frames").SafeGetInt64(0))
							{
								Best.GetExtras().Set("b_frames", FVariantValue((int64) 1));
							}
						}
						OutStreamInfo = Best;
					}
					else
					{
						if (MaxWidth == 0)
						{
							MaxWidth = 32768;
						}
						if (MaxHeight == 0)
						{
							MaxHeight = 32768;
						}
						FStreamCodecInformation		Best;
						bool bFirst = true;
						for(int32 i=0; i<VideoCodecInfos.Num(); ++i)
						{
							const FStreamCodecInformation::FResolution& Res = VideoCodecInfos[i].GetResolution();
							if (Res.ExceedsLimit(MaxWidth, MaxHeight))
							{
								continue;
							}
							if (bFirst)
							{
								bFirst = false;
								Best = VideoCodecInfos[i];
							}
							if (Res.Width > Best.GetResolution().Width)
							{
								Best.SetResolution(FStreamCodecInformation::FResolution(Res.Width, Best.GetResolution().Height));
							}
							if (Res.Height > Best.GetResolution().Height)
							{
								Best.SetResolution(FStreamCodecInformation::FResolution(Best.GetResolution().Width, Res.Height));
							}
							// Note: the final RFC 6381 codec string will be bogus since we do not re-create it here.
							if (VideoCodecInfos[i].GetProfile() > Best.GetProfile())
							{
								Best.SetProfile(VideoCodecInfos[i].GetProfile());
							}
							if (VideoCodecInfos[i].GetProfileLevel() > Best.GetProfileLevel())
							{
								Best.SetProfileLevel(VideoCodecInfos[i].GetProfileLevel());
							}
							if (VideoCodecInfos[i].GetExtras().GetValue("b_frames").SafeGetInt64(0))
							{
								Best.GetExtras().Set("b_frames", FVariantValue((int64) 1));
							}
						}
						// Found none? (resolution limit set too low)
						if (bFirst)
						{
							// Find smallest by bandwidth
							Best = VideoCodecInfos[0];
							int32 BestBW = VideoSet->GetRepresentationByIndex(0)->GetBitrate();
							for(int32 i=1; i<VideoCodecInfos.Num(); ++i)
							{
								if (VideoSet->GetRepresentationByIndex(i)->GetBitrate() < BestBW)
								{
									Best = VideoCodecInfos[i];
									BestBW = VideoSet->GetRepresentationByIndex(i)->GetBitrate();
								}
							}
						}
						OutStreamInfo = Best;
					}
					return true;
				}
			}
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
/**
 * Creates a decoder for the specified stream type based on the first access
 * unit's format.
 *
 * @return
 */
int32 FAdaptiveStreamingPlayer::CreateDecoder(EStreamType type)
{
	if (type == EStreamType::Video)
	{
		if (VideoDecoder.Decoder == nullptr)
		{
			FAccessUnit* AccessUnit = nullptr;
			bool bOk = MultiStreamBufferVid.PeekAndAddRef(AccessUnit);
			if (AccessUnit)
			{
				FTimeValue DecodeTime = AccessUnit->PTS;
				VideoDecoder.CurrentCodecInfo.Clear();
				if (AccessUnit->AUCodecData.IsValid())
				{
					VideoDecoder.CurrentCodecInfo = AccessUnit->AUCodecData->ParsedInfo;
				}
				FAccessUnit::Release(AccessUnit);

				// Get the largest stream resolution of the currently selected video adaptation set.
				// This is only an initial selection as there could be other adaptation sets in upcoming periods
				// that have a larger resolution that is still within the allowed limits.
				FStreamCodecInformation HighestStream;
				if (!FindMatchingStreamInfo(HighestStream, DecodeTime, 0, 0))
				{
					FErrorDetail err;
					err.SetFacility(Facility::EFacility::Player);
					err.SetMessage("Stream information not found when creating video decoder");
					err.SetCode(INTERR_NO_STREAM_INFORMATION);
					PostError(err);
					return -1;
				}

				if (VideoDecoder.CurrentCodecInfo.GetCodec() == FStreamCodecInformation::ECodec::H264)
				{
					// Create H.264 video decoder
					IVideoDecoderH264* DecoderH264 = IVideoDecoderH264::Create();
					if (DecoderH264)
					{
						VideoDecoder.Decoder = DecoderH264;
						VideoDecoder.Parent = this;
						DecoderH264->SetPlayerSessionServices(this);
						IVideoDecoderH264::FInstanceConfiguration h264Cfg = PlayerConfig.DecoderCfg264;
						h264Cfg.ProfileIdc = HighestStream.GetProfile();
						h264Cfg.LevelIdc = HighestStream.GetProfileLevel();
						h264Cfg.MaxFrameWidth = HighestStream.GetResolution().Width;
						h264Cfg.MaxFrameHeight = HighestStream.GetResolution().Height;
						h264Cfg.AdditionalOptions = HighestStream.GetExtras();

						// Add in any player options that are for decoder use
						TArray<FString> DecoderOptionKeys;
						PlayerOptions.GetKeysStartingWith("videoDecoder", DecoderOptionKeys);
						for (const FString & Key : DecoderOptionKeys)
						{
							h264Cfg.AdditionalOptions.Set(Key, PlayerOptions.GetValue(Key));
						}

						// Attach video decoder buffer monitor.
						DecoderH264->SetAUInputBufferListener(&VideoDecoder);
						DecoderH264->SetReadyBufferListener(&VideoDecoder);
						// Have the video decoder send its output to the video renderer
						DecoderH264->SetRenderer(VideoRender.Renderer);
						// Hand it (may be nullptr) a delegate for platform for resource queries
						DecoderH264->SetResourceDelegate(VideoDecoderResourceDelegate.Pin());
						// Open the decoder after having set all listeners.
						DecoderH264->Open(h264Cfg);
					}
				}
#if ELECTRA_PLATFORM_HAS_H265_DECODER
				else if (VideoDecoder.CurrentCodecInfo.GetCodec() == FStreamCodecInformation::ECodec::H265)
				{
					// Create H.265 video decoder
					IVideoDecoderH265* DecoderH265 = IVideoDecoderH265::Create();
					if (DecoderH265)
					{
						VideoDecoder.Decoder = DecoderH265;
						VideoDecoder.Parent = this;
						DecoderH265->SetPlayerSessionServices(this);
						IVideoDecoderH265::FInstanceConfiguration h265Cfg;// = PlayerConfig.DecoderCfg265;
						h265Cfg.Tier = HighestStream.GetProfileTier();
						h265Cfg.Profile = HighestStream.GetProfile();
						h265Cfg.Level = HighestStream.GetProfileLevel();
						h265Cfg.MaxFrameWidth = HighestStream.GetResolution().Width;
						h265Cfg.MaxFrameHeight = HighestStream.GetResolution().Height;
						h265Cfg.AdditionalOptions = HighestStream.GetExtras();

						// Add in any player options that are for decoder use
						TArray<FString> DecoderOptionKeys;
						PlayerOptions.GetKeysStartingWith("videoDecoder", DecoderOptionKeys);
						for (const FString & Key : DecoderOptionKeys)
						{
							h265Cfg.AdditionalOptions.Set(Key, PlayerOptions.GetValue(Key));
						}

						// Attach video decoder buffer monitor.
						DecoderH265->SetAUInputBufferListener(&VideoDecoder);
						DecoderH265->SetReadyBufferListener(&VideoDecoder);
						// Have the video decoder send its output to the video renderer
						DecoderH265->SetRenderer(VideoRender.Renderer);
						// Hand it (may be nullptr) a delegate for platform for resource queries
						DecoderH265->SetResourceDelegate(VideoDecoderResourceDelegate.Pin());
						// Open the decoder after having set all listeners.
						DecoderH265->Open(h265Cfg);
					}
				}
#endif

				if (VideoDecoder.Decoder)
				{
					// Now we get the currently limited stream resolution and let the decoder now what we will be using
					// at most right now. This allows the decoder to be created with a smaller memory footprint at first.
					UpdateStreamResolutionLimit();
				}
				else
				{
					FErrorDetail err;
					err.SetFacility(Facility::EFacility::Player);
					err.SetMessage("Unsupported video codec");
					err.SetCode(INTERR_UNSUPPORTED_CODEC);
					PostError(err);
					return -1;
				}
			}
		}
		return 0;
	}
	else if (type == EStreamType::Audio)
	{
		if (AudioDecoder.Decoder == nullptr)
		{
			FAccessUnit* AccessUnit = nullptr;
			bool bOk = MultiStreamBufferAud.PeekAndAddRef(AccessUnit);
			if (AccessUnit)
			{
				AudioDecoder.CurrentCodecInfo.Clear();
				if (AccessUnit->AUCodecData.IsValid())
				{
					AudioDecoder.CurrentCodecInfo = AccessUnit->AUCodecData->ParsedInfo;
				}
				FAccessUnit::Release(AccessUnit);

				if (AudioDecoder.CurrentCodecInfo.GetCodec() == FStreamCodecInformation::ECodec::AAC)
				{
					// Create an AAC audio decoder
					AudioDecoder.Decoder = IAudioDecoderAAC::Create();
					AudioDecoder.Decoder->SetPlayerSessionServices(this);
					AudioDecoder.Parent = this;
					// Attach buffer monitors.
					AudioDecoder.Decoder->SetAUInputBufferListener(&AudioDecoder);
					AudioDecoder.Decoder->SetReadyBufferListener(&AudioDecoder);
					// Have to audio decoder send its output to the audio renderer
					AudioDecoder.Decoder->SetRenderer(AudioRender.Renderer);
					// Open the decoder after having set all listeners.
					AudioDecoder.Decoder->Open(PlayerConfig.DecoderCfgAAC);
				}

				if (!AudioDecoder.Decoder)
				{
					FErrorDetail err;
					err.SetFacility(Facility::EFacility::Player);
					err.SetMessage("Unsupported audio codec");
					err.SetCode(INTERR_UNSUPPORTED_CODEC);
					PostError(err);
					return -1;
				}
			}
		}
		return 0;
	}
	return -1;
}


//-----------------------------------------------------------------------------
/**
 * Destroys the decoders.
 */
void FAdaptiveStreamingPlayer::DestroyDecoders()
{
/*
NOTE: We do not clear out the renderers from the decoder. On their way down the decoders should still be able
      to access the renderer without harm and dispatch their last remaining data.

	if (mVideoDecoder.Decoder)
		mVideoDecoder.Decoder->SetRenderer(nullptr);
	if (mAudioDecoder.Decoder)
		mAudioDecoder.Decoder->SetRenderer(nullptr);
*/
	AudioDecoder.Close();
	VideoDecoder.Close();
}


//-----------------------------------------------------------------------------
/**
 * Check if the decoders need to change.
 */
void FAdaptiveStreamingPlayer::HandleDecoderChanges()
{
	if (VideoDecoder.bDrainingForCodecChange && VideoDecoder.bDrainingForCodecChangeDone)
	{
		VideoDecoder.Close();
		VideoDecoder.bDrainingForCodecChange = false;
		VideoDecoder.bDrainingForCodecChangeDone = false;
	}
	CreateDecoder(EStreamType::Video);
	CreateDecoder(EStreamType::Audio);
}


void FAdaptiveStreamingPlayer::VideoDecoderInputNeeded(const IAccessUnitBufferListener::FBufferStats &currentInputBufferStats)
{
	DiagnosticsCriticalSection.Lock();
	VideoBufferStats.DecoderInputBuffer = currentInputBufferStats;
	DiagnosticsCriticalSection.Unlock();
	if (!VideoDecoder.bDrainingForCodecChange)
	{
		EDecoderState decState = DecoderState;
		if (decState == EDecoderState::eDecoder_Running)
		{
			FeedDecoder(EStreamType::Video, MultiStreamBufferVid, VideoDecoder.Decoder);
		}
	}
}

void FAdaptiveStreamingPlayer::VideoDecoderOutputReady(const IDecoderOutputBufferListener::FDecodeReadyStats &currentReadyStats)
{
	DiagnosticsCriticalSection.Lock();
	VideoBufferStats.DecoderOutputBuffer = currentReadyStats;
	DiagnosticsCriticalSection.Unlock();
}


void FAdaptiveStreamingPlayer::AudioDecoderInputNeeded(const IAccessUnitBufferListener::FBufferStats &currentInputBufferStats)
{
	DiagnosticsCriticalSection.Lock();
	AudioBufferStats.DecoderInputBuffer = currentInputBufferStats;
	DiagnosticsCriticalSection.Unlock();
	EDecoderState decState = DecoderState;
	if (decState == EDecoderState::eDecoder_Running)
	{
		FeedDecoder(EStreamType::Audio, MultiStreamBufferAud, AudioDecoder.Decoder);
	}
}

void FAdaptiveStreamingPlayer::AudioDecoderOutputReady(const IDecoderOutputBufferListener::FDecodeReadyStats &currentReadyStats)
{
	DiagnosticsCriticalSection.Lock();
	AudioBufferStats.DecoderOutputBuffer = currentReadyStats;
	DiagnosticsCriticalSection.Unlock();
}



//-----------------------------------------------------------------------------
/**
 * Sends an available AU to a decoder.
 * If the current buffer level is below the underrun threshold an underrun
 * message is sent to the worker thread.
 *
 * @param Type
 * @param FromMultistreamBuffer
 * @param Decoder
 */
void FAdaptiveStreamingPlayer::FeedDecoder(EStreamType Type, FMultiTrackAccessUnitBuffer& FromMultistreamBuffer, IAccessUnitBufferInterface* Decoder)
{
	// Lock the AU buffer for the duration of this function to ensure this can never clash with a Flush() call
	// since we are checking size, eod state and subsequently popping an AU, for which the buffer must stay consistent inbetween!
	// Also to ensure the active buffer doesn't get changed from one track to another.
	FMultiTrackAccessUnitBuffer::FScopedLock lock(FromMultistreamBuffer);

	FBufferStats* pStats = nullptr;
	Metrics::FDataAvailabilityChange* pAvailability = nullptr;
	FStreamCodecInformation* CurrentCodecInfo = nullptr;
	bool bCodecChangeDetected = false;

	switch(Type)
	{
		case EStreamType::Video:
			pStats = &VideoBufferStats;
			pAvailability = &DataAvailabilityStateVid;
			CurrentCodecInfo = &VideoDecoder.CurrentCodecInfo;
			break;
		case EStreamType::Audio:
			pStats = &AudioBufferStats;
			pAvailability = &DataAvailabilityStateAud;
			CurrentCodecInfo = &AudioDecoder.CurrentCodecInfo;
			break;
		case EStreamType::Subtitle:
			pStats = &TextBufferStats;
			pAvailability = &DataAvailabilityStateTxt;
			break;
		default:
			checkNoEntry();
			return;
	}

	// Is the buffer (the Type of elementary stream actually) active/selected?
	if (!FromMultistreamBuffer.IsDeselected())
	{
		// Check for buffer underrun.
		if (!bRebufferPending && CurrentState == EPlayerState::eState_Playing && StreamState == EStreamState::eStream_Running && PipelineState == EPipelineState::ePipeline_Running)
		{
			bool bEODSet = FromMultistreamBuffer.IsEODFlagSet();
			if (!bEODSet && FromMultistreamBuffer.Num() == 0)
			{
				// Buffer underrun.
				bRebufferPending = true;
				FTimeValue LastKnownPTS = FromMultistreamBuffer.GetLastPoppedPTS();
				// Only set the 'rebuffer at' time if we have a valid last known PTS. If we don't
				// then maybe this is a cascade failure from a previous rebuffer attempt for which
				// we then try that time once more.
				if (LastKnownPTS.IsValid())
				{
					RebufferDetectedAtPlayPos = LastKnownPTS;
				}
				WorkerThread.SendMessage(FWorkerThread::FMessage::EType::BufferUnderrun);
			}
		}

		FAccessUnit* AccessUnit = nullptr;
		FromMultistreamBuffer.PeekAndAddRef(AccessUnit);
		if (AccessUnit)
		{
			// Change in codec?
			if (CurrentCodecInfo && AccessUnit->AUCodecData.IsValid() && AccessUnit->AUCodecData->ParsedInfo.GetCodec() != CurrentCodecInfo->GetCodec())
			{
				bCodecChangeDetected = true;
				if (Decoder)
				{
					// Check type of stream. We can currently change the video codec only.
					if (Type == EStreamType::Video)
					{
						if (VideoDecoder.Decoder && !VideoDecoder.bDrainingForCodecChange)
						{
							VideoDecoder.bDrainingForCodecChange = true;
							VideoDecoder.Decoder->DrainForCodecChange();
						}
					}
					else
					{
						FErrorDetail err;
						err.SetFacility(Facility::EFacility::Player);
						err.SetMessage("Codec change not supported");
						err.SetCode(INTERR_CODEC_CHANGE_NOT_SUPPORTED);
						PostError(err);
					}
				}
			}
			else
			{
				if (Type == EStreamType::Video && VideoDecoder.bApplyNewLimits)
				{
					FStreamCodecInformation StreamInfo;
					if (FindMatchingStreamInfo(StreamInfo, AccessUnit->PTS, VideoResolutionLimitWidth, VideoResolutionLimitHeight))
					{
						if (VideoDecoder.Decoder)
						{
							if (VideoDecoder.CurrentCodecInfo.GetCodec() == FStreamCodecInformation::ECodec::H264 && StreamInfo.GetCodec() == FStreamCodecInformation::ECodec::H264)
							{
								static_cast<IVideoDecoderH264*>(VideoDecoder.Decoder)->SetMaximumDecodeCapability(StreamInfo.GetResolution().Width, StreamInfo.GetResolution().Height, StreamInfo.GetProfile(), StreamInfo.GetProfileLevel(), StreamInfo.GetExtras());
							}
						}
					}
					VideoDecoder.bApplyNewLimits = false;
				}

				FromMultistreamBuffer.Pop(AccessUnit);
				if (Decoder)
				{
				// The decoder has asked to be fed a new AU so it better be able to accept it.
					/*IAccessUnitBufferInterface::EAUpushResult auPushRes =*/ Decoder->AUdataPushAU(AccessUnit);
				}

				// The decoder will have added a ref count if it took the AU. If it didnt' for whatever reason
				// we still release it to get rid of it and not cause a memory leak.
				FAccessUnit::Release(AccessUnit);
				AccessUnit = nullptr;

				if (pAvailability)
				{
					UpdateDataAvailabilityState(*pAvailability, Metrics::FDataAvailabilityChange::EAvailability::DataAvailable);
				}
			}
			// Release ref count from peeking.
			FAccessUnit::Release(AccessUnit);
		}
	}
	// An AU is not tagged as being "the last" one. Instead the EOD is handled separately and must be dealt with
	// by the decoders accordingly.
	if (!bCodecChangeDetected && FromMultistreamBuffer.IsEODFlagSet() && FromMultistreamBuffer.Num() == 0)
	{
		if (pStats && !pStats->DecoderInputBuffer.bEODSignaled)
		{
			if (Decoder)
			{
				Decoder->AUdataPushEOD();
			}
		}
		if (pAvailability)
		{
			UpdateDataAvailabilityState(*pAvailability, Metrics::FDataAvailabilityChange::EAvailability::DataNotAvailable);
		}
	}
}



} // namespace Electra


