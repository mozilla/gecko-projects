/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WaveDemuxer.h"
#include "MediaContainerType.h"
#include "MediaDecoderStateMachine.h"
#include "WaveDecoder.h"
#include "MediaFormatReader.h"
#include "PDMFactory.h"

namespace mozilla {

MediaDecoder*
WaveDecoder::Clone(MediaDecoderOwner* aOwner)
{
  return new WaveDecoder(aOwner);
}

MediaDecoderStateMachine*
WaveDecoder::CreateStateMachine()
{
  return new MediaDecoderStateMachine(
    this, new MediaFormatReader(this, new WAVDemuxer(GetResource())));
}

/* static */ bool
WaveDecoder::IsSupportedType(const MediaContainerType& aContainerType)
{
  if (!IsWaveEnabled()) {
    return false;
  }
  if (aContainerType.Type() == MEDIAMIMETYPE("audio/wave")
      || aContainerType.Type() == MEDIAMIMETYPE("audio/x-wav")
      || aContainerType.Type() == MEDIAMIMETYPE("audio/wav")
      || aContainerType.Type() == MEDIAMIMETYPE("audio/x-pn-wav")) {
    return (aContainerType.ExtendedType().Codecs().IsEmpty()
            || aContainerType.ExtendedType().Codecs().AsString().EqualsASCII("1")
            || aContainerType.ExtendedType().Codecs().AsString().EqualsASCII("6")
            || aContainerType.ExtendedType().Codecs().AsString().EqualsASCII("7"));
  }

  return false;
}

} // namespace mozilla
