/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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

#include "LibWebRTCDtlsTransportBackend.h"
#include "RTCSctpTransportBackend.h"

namespace WebCore {
class LibWebRTCSctpTransportBackendObserver;
class LibWebRTCSctpTransportBackend final : public RTCSctpTransportBackend, public CanMakeWeakPtr<LibWebRTCSctpTransportBackend> {
    WTF_MAKE_TZONE_ALLOCATED(LibWebRTCSctpTransportBackend);
public:
    explicit LibWebRTCSctpTransportBackend(Ref<webrtc::SctpTransportInterface>&&, Ref<webrtc::DtlsTransportInterface>&&);
    ~LibWebRTCSctpTransportBackend();

private:
    // RTCSctpTransportBackend
    const void* backend() const final { return m_backend.ptr(); }
    UniqueRef<RTCDtlsTransportBackend> dtlsTransportBackend() final;
    void registerClient(RTCSctpTransportBackendClient&) final;
    void unregisterClient() final;

    const Ref<webrtc::SctpTransportInterface> m_backend;
    const Ref<webrtc::DtlsTransportInterface> m_dtlsBackend;
    const RefPtr<LibWebRTCSctpTransportBackendObserver> m_observer;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(LIBWEBRTC)
