/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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

#include "config.h"
#include "LibWebRTCDtlsTransportBackend.h"

#if ENABLE(WEB_RTC) && USE(LIBWEBRTC)

#include "LibWebRTCIceTransportBackend.h"
#include "LibWebRTCProvider.h"
#include <JavaScriptCore/ArrayBuffer.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

static inline RTCDtlsTransportState toRTCDtlsTransportState(webrtc::DtlsTransportState state)
{
    switch (state) {
    case webrtc::DtlsTransportState::kNew:
        return RTCDtlsTransportState::New;
    case webrtc::DtlsTransportState::kConnecting:
        return RTCDtlsTransportState::Connecting;
    case webrtc::DtlsTransportState::kConnected:
        return RTCDtlsTransportState::Connected;
    case webrtc::DtlsTransportState::kClosed:
        return RTCDtlsTransportState::Closed;
    case webrtc::DtlsTransportState::kFailed:
        return RTCDtlsTransportState::Failed;
    case webrtc::DtlsTransportState::kNumValues:
        ASSERT_NOT_REACHED();
        return RTCDtlsTransportState::Failed;
    }

    RELEASE_ASSERT_NOT_REACHED();
}

class LibWebRTCDtlsTransportBackendObserver final : public ThreadSafeRefCounted<LibWebRTCDtlsTransportBackendObserver>, public webrtc::DtlsTransportObserverInterface {
public:
    static Ref<LibWebRTCDtlsTransportBackendObserver> create(RTCDtlsTransportBackendClient& client, Ref<webrtc::DtlsTransportInterface>&& backend) { return adoptRef(*new LibWebRTCDtlsTransportBackendObserver(client, WTFMove(backend))); }

    void start();
    void stop();

private:
    LibWebRTCDtlsTransportBackendObserver(RTCDtlsTransportBackendClient&, Ref<webrtc::DtlsTransportInterface>&&);

    void OnStateChange(webrtc::DtlsTransportInformation) final;
    void OnError(webrtc::RTCError) final;

    void updateState(webrtc::DtlsTransportInformation&&);

    const Ref<webrtc::DtlsTransportInterface> m_backend;
    WeakPtr<RTCDtlsTransportBackendClient> m_client;
};

LibWebRTCDtlsTransportBackendObserver::LibWebRTCDtlsTransportBackendObserver(RTCDtlsTransportBackendClient& client, Ref<webrtc::DtlsTransportInterface>&& backend)
    : m_backend(WTFMove(backend))
    , m_client(client)
{
}

void LibWebRTCDtlsTransportBackendObserver::updateState(webrtc::DtlsTransportInformation&& info)
{
    if (!m_client)
        return;

    Vector<webrtc::Buffer> certificates;
    if (auto* remoteCertificates = info.remote_ssl_certificates()) {
        for (size_t i = 0; i < remoteCertificates->GetSize(); ++i) {
            webrtc::Buffer certificate;
            remoteCertificates->Get(i).ToDER(&certificate);
            certificates.append(WTFMove(certificate));
        }
    }
    m_client->onStateChanged(toRTCDtlsTransportState(info.state()), map(certificates, [](auto& certificate) -> Ref<JSC::ArrayBuffer> {
        return JSC::ArrayBuffer::create(certificate);
    }));
}

void LibWebRTCDtlsTransportBackendObserver::start()
{
    LibWebRTCProvider::callOnWebRTCNetworkThread([this, protectedThis = Ref { *this }]() mutable {
        m_backend->RegisterObserver(this);
        callOnMainThread([protectedThis = WTFMove(protectedThis), info = m_backend->Information()]() mutable {
            protectedThis->updateState(WTFMove(info));
        });
    });
}

void LibWebRTCDtlsTransportBackendObserver::stop()
{
    m_client = nullptr;
    LibWebRTCProvider::callOnWebRTCNetworkThread([protectedThis = Ref { *this }] {
        protectedThis->m_backend->UnregisterObserver();
    });
}

void LibWebRTCDtlsTransportBackendObserver::OnStateChange(webrtc::DtlsTransportInformation info)
{
    callOnMainThread([protectedThis = Ref { *this }, info = WTFMove(info)]() mutable {
        protectedThis->updateState(WTFMove(info));
    });
}

void LibWebRTCDtlsTransportBackendObserver::OnError(webrtc::RTCError)
{
    callOnMainThread([protectedThis = Ref { *this }] {
        if (protectedThis->m_client)
            protectedThis->m_client->onError();
    });
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(LibWebRTCDtlsTransportBackend);

LibWebRTCDtlsTransportBackend::LibWebRTCDtlsTransportBackend(Ref<webrtc::DtlsTransportInterface>&& backend)
    : m_backend(WTFMove(backend))
{
}

LibWebRTCDtlsTransportBackend::~LibWebRTCDtlsTransportBackend()
{
    if (m_observer)
        m_observer->stop();
}

UniqueRef<RTCIceTransportBackend> LibWebRTCDtlsTransportBackend::iceTransportBackend()
{
    return makeUniqueRef<LibWebRTCIceTransportBackend>(m_backend->ice_transport());
}

void LibWebRTCDtlsTransportBackend::registerClient(RTCDtlsTransportBackendClient& client)
{
    ASSERT(!m_observer);
    lazyInitialize(m_observer, LibWebRTCDtlsTransportBackendObserver::create(client, m_backend.get()));
    m_observer->start();
}

void LibWebRTCDtlsTransportBackend::unregisterClient()
{
    ASSERT(m_observer);
    m_observer->stop();
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(LIBWEBRTC)
