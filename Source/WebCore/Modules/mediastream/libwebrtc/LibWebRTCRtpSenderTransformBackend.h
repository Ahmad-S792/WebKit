/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(WEB_RTC) && USE(LIBWEBRTC)

#include "LibWebRTCRtpSenderBackend.h"
#include "LibWebRTCRtpTransformBackend.h"

namespace WebCore {

class LibWebRTCSenderTransformer;

class LibWebRTCRtpSenderTransformBackend final : public LibWebRTCRtpTransformBackend {
    WTF_MAKE_TZONE_ALLOCATED(LibWebRTCRtpSenderTransformBackend);
public:
    static Ref<LibWebRTCRtpSenderTransformBackend> create(Ref<webrtc::RtpSenderInterface>&& sender) { return adoptRef(*new LibWebRTCRtpSenderTransformBackend(WTFMove(sender))); }
    ~LibWebRTCRtpSenderTransformBackend();

private:
    explicit LibWebRTCRtpSenderTransformBackend(Ref<webrtc::RtpSenderInterface>&&);

    // RTCRtpTransformBackend
    void setTransformableFrameCallback(Callback&&) final;
    bool requestKeyFrame(const String&) final;

    bool m_isRegistered { false };
    const Ref<webrtc::RtpSenderInterface> m_rtcSender;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(LIBWEBRTC)
