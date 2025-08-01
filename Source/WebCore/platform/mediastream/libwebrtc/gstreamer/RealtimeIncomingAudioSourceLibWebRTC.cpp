/*
 * Copyright (C) 2017 Igalia Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if USE(LIBWEBRTC) && USE(GSTREAMER)
#include "RealtimeIncomingAudioSourceLibWebRTC.h"

#include "LibWebRTCAudioFormat.h"
#include "gstreamer/GStreamerAudioData.h"
#include "gstreamer/GStreamerAudioStreamDescription.h"

namespace WebCore {

Ref<RealtimeIncomingAudioSource> RealtimeIncomingAudioSource::create(Ref<webrtc::AudioTrackInterface>&& audioTrack, String&& audioTrackId)
{
    auto source = RealtimeIncomingAudioSourceLibWebRTC::create(WTFMove(audioTrack), WTFMove(audioTrackId));
    source->start();
    return source;
}

Ref<RealtimeIncomingAudioSourceLibWebRTC> RealtimeIncomingAudioSourceLibWebRTC::create(Ref<webrtc::AudioTrackInterface>&& audioTrack, String&& audioTrackId)
{
    return adoptRef(*new RealtimeIncomingAudioSourceLibWebRTC(WTFMove(audioTrack), WTFMove(audioTrackId)));
}

RealtimeIncomingAudioSourceLibWebRTC::RealtimeIncomingAudioSourceLibWebRTC(Ref<webrtc::AudioTrackInterface>&& audioTrack, String&& audioTrackId)
    : RealtimeIncomingAudioSource(WTFMove(audioTrack), WTFMove(audioTrackId))
{
}

void RealtimeIncomingAudioSourceLibWebRTC::OnData(const void* audioData, int, int sampleRate, size_t numberOfChannels, size_t numberOfFrames)
{
    GstAudioInfo info;
    GstAudioFormat format = gst_audio_format_build_integer(
        LibWebRTCAudioFormat::isSigned,
        LibWebRTCAudioFormat::isBigEndian ? G_BIG_ENDIAN : G_LITTLE_ENDIAN,
        LibWebRTCAudioFormat::sampleSize,
        LibWebRTCAudioFormat::sampleSize);

    gst_audio_info_set_format(&info, format, sampleRate, numberOfChannels, NULL);

    auto bufferSize = GST_AUDIO_INFO_BPF(&info) * numberOfFrames;
    auto buffer = adoptGRef(gst_buffer_new_memdup(const_cast<gpointer>(audioData), bufferSize));
    gst_buffer_add_audio_meta(buffer.get(), &info, numberOfFrames, nullptr);
    auto caps = adoptGRef(gst_audio_info_to_caps(&info));

    if (m_baseTime == MediaTime::invalidTime())
        m_baseTime = MediaTime::createWithSeconds(MonotonicTime::now().secondsSinceEpoch());

    MediaTime mediaTime = m_baseTime + MediaTime((m_numberOfFrames * G_USEC_PER_SEC) / sampleRate, G_USEC_PER_SEC);
    GST_BUFFER_PTS(buffer.get()) = toGstUnsigned64Time(mediaTime);

    auto sample = adoptGRef(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr));
    GStreamerAudioData data(WTFMove(sample), info);
    audioSamplesAvailable(mediaTime, data, GStreamerAudioStreamDescription(info), numberOfFrames);

    m_numberOfFrames += numberOfFrames;
}
}

#endif // USE(LIBWEBRTC)
