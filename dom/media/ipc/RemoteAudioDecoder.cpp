/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RemoteAudioDecoder.h"

#include "RemoteDecoderManagerChild.h"
#include "VorbisDecoder.h"
#include "mozilla/PodOperations.h"

namespace mozilla {

RemoteAudioDecoderChild::RemoteAudioDecoderChild() : RemoteDecoderChild() {}

mozilla::ipc::IPCResult RemoteAudioDecoderChild::RecvOutput(
    const DecodedOutputIPDL& aDecodedData) {
  AssertOnManagerThread();
  MOZ_ASSERT(aDecodedData.type() == DecodedOutputIPDL::TRemoteAudioDataIPDL);
  const RemoteAudioDataIPDL& aData = aDecodedData.get_RemoteAudioDataIPDL();

  AlignedAudioBuffer alignedAudioBuffer;
  alignedAudioBuffer.SetLength(aData.buffer().Size<AudioDataValue>());
  PodCopy(alignedAudioBuffer.Data(), aData.buffer().get<AudioDataValue>(),
          alignedAudioBuffer.Length());

  DeallocShmem(aData.buffer());

  RefPtr<AudioData> audio = new AudioData(
      aData.base().offset(), aData.base().time(), std::move(alignedAudioBuffer),
      aData.channels(), aData.rate(), aData.channelMap());

  mDecodedData.AppendElement(std::move(audio));
  return IPC_OK();
}

MediaResult RemoteAudioDecoderChild::InitIPDL(
    const AudioInfo& aAudioInfo,
    const CreateDecoderParams::OptionSet& aOptions) {
  RefPtr<RemoteDecoderManagerChild> manager =
      RemoteDecoderManagerChild::GetSingleton();

  // The manager isn't available because RemoteDecoderManagerChild has been
  // initialized with null end points and we don't want to decode video on RDD
  // process anymore. Return false here so that we can fallback to other PDMs.
  if (!manager) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("RemoteDecoderManager is not available."));
  }

  if (!manager->CanSend()) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("RemoteDecoderManager unable to send."));
  }

  mIPDLSelfRef = this;
  bool success = false;
  nsCString errorDescription;
  if (manager->SendPRemoteDecoderConstructor(this, aAudioInfo, aOptions,
                                             &success, &errorDescription)) {
    mCanSend = true;
  }

  return success ? MediaResult(NS_OK)
                 : MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, errorDescription);
}

RemoteAudioDecoderParent::RemoteAudioDecoderParent(
    RemoteDecoderManagerParent* aParent, const AudioInfo& aAudioInfo,
    const CreateDecoderParams::OptionSet& aOptions,
    TaskQueue* aManagerTaskQueue, TaskQueue* aDecodeTaskQueue, bool* aSuccess,
    nsCString* aErrorDescription)
    : RemoteDecoderParent(aParent, aManagerTaskQueue, aDecodeTaskQueue),
      mAudioInfo(aAudioInfo) {
  CreateDecoderParams params(mAudioInfo);
  params.mTaskQueue = mDecodeTaskQueue;
  params.mOptions = aOptions;
  MediaResult error(NS_OK);
  params.mError = &error;

  if (VorbisDataDecoder::IsVorbis(params.mConfig.mMimeType)) {
    mDecoder = new VorbisDataDecoder(params);
  }

  if (NS_FAILED(error)) {
    MOZ_ASSERT(aErrorDescription);
    *aErrorDescription = error.Description();
  }

  *aSuccess = !!mDecoder;
}

MediaResult RemoteAudioDecoderParent::ProcessDecodedData(
    const MediaDataDecoder::DecodedData& aData) {
  MOZ_ASSERT(OnManagerThread());

  for (const auto& data : aData) {
    MOZ_ASSERT(data->mType == MediaData::Type::AUDIO_DATA,
               "Can only decode audio using RemoteAudioDecoderParent!");
    AudioData* audio = static_cast<AudioData*>(data.get());

    MOZ_ASSERT(audio->Data().Elements(),
               "Decoded audio must output an AlignedAudioBuffer "
               "to be used with RemoteAudioDecoderParent");

    Shmem buffer;
    if (!AllocShmem(audio->Data().Length() * sizeof(AudioDataValue),
                    Shmem::SharedMemory::TYPE_BASIC, &buffer)) {
      return MediaResult(NS_ERROR_OUT_OF_MEMORY,
                         "AllocShmem failed in "
                         "RemoteAudioDecoderParent::ProcessDecodedData");
    }
    if (audio->Data().Length() > buffer.Size<AudioDataValue>()) {
      return MediaResult(NS_ERROR_OUT_OF_MEMORY,
                         "AllocShmem returned less than requested in "
                         "RemoteAudioDecoderParent::ProcessDecodedData");
    }

    PodCopy(buffer.get<AudioDataValue>(), audio->Data().Elements(),
            audio->Data().Length());

    RemoteAudioDataIPDL output(
        MediaDataIPDL(data->mOffset, data->mTime, data->mTimecode,
                      data->mDuration, data->mKeyframe),
        audio->mChannels, audio->mRate, audio->mChannelMap, std::move(buffer));

    Unused << SendOutput(output);
  }

  return NS_OK;
}

}  // namespace mozilla
