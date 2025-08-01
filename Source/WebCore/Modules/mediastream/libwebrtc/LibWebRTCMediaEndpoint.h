/*
 * Copyright (C) 2017-2025 Apple Inc. All rights reserved.
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

#include "LibWebRTCObservers.h"
#include "LibWebRTCProvider.h"
#include "LibWebRTCRefWrappers.h"
#include "LibWebRTCRtpSenderBackend.h"
#include "RTCRtpReceiver.h"
#include "Timer.h"
#include <wtf/WeakRef.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN

#include <webrtc/api/jsep.h>
#include <webrtc/api/peer_connection_interface.h>
// See Bug 274508: Disable thread-safety-reference-return warnings in libwebrtc
IGNORE_CLANG_WARNINGS_BEGIN("thread-safety-reference-return")
IGNORE_CLANG_WARNINGS_BEGIN("nullability-completeness")
#include <webrtc/pc/peer_connection_factory.h>
#include <webrtc/pc/rtc_stats_collector.h>
IGNORE_CLANG_WARNINGS_END
IGNORE_CLANG_WARNINGS_END

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

#include <wtf/LoggerHelper.h>
#include <wtf/RobinHoodHashMap.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace webrtc {
class CreateSessionDescriptionObserver;
class DataChannelInterface;
class IceCandidateInterface;
class MediaStreamInterface;
class PeerConnectionObserver;
class SessionDescriptionInterface;
class SetSessionDescriptionObserver;
}

namespace WebCore {
class LibWebRTCProvider;
class LibWebRTCPeerConnectionBackend;
class LibWebRTCRtpReceiverBackend;
class LibWebRTCRtpTransceiverBackend;
class LibWebRTCStatsCollector;
class MediaStreamTrack;
class RTCSessionDescription;

struct LibWebRTCMediaEndpointTransceiverState;

class LibWebRTCMediaEndpoint
    : public ThreadSafeRefCounted<LibWebRTCMediaEndpoint, WTF::DestructionThread::Main>
    , private webrtc::PeerConnectionObserver
    , private webrtc::RTCStatsCollectorCallback
#if !RELEASE_LOG_DISABLED
    , private LoggerHelper
#endif
{
public:
    static Ref<LibWebRTCMediaEndpoint> create(LibWebRTCPeerConnectionBackend& peerConnection, LibWebRTCProvider& client) { return adoptRef(*new LibWebRTCMediaEndpoint(peerConnection, client)); }
    virtual ~LibWebRTCMediaEndpoint() = default;

    void restartIce();
    bool setConfiguration(LibWebRTCProvider&, webrtc::PeerConnectionInterface::RTCConfiguration&&);

    webrtc::PeerConnectionInterface& backend() const { ASSERT(m_backend); return *m_backend.get(); }
    void doSetLocalDescription(const RTCSessionDescription*);
    void doSetRemoteDescription(const RTCSessionDescription&);
    void doCreateOffer(const RTCOfferOptions&);
    void doCreateAnswer();
    void gatherDecoderImplementationName(Function<void(String&&)>&&);
    void getStats(Ref<DeferredPromise>&&);
    void getStats(webrtc::RtpReceiverInterface&, Ref<DeferredPromise>&&);
    void getStats(webrtc::RtpSenderInterface&, Ref<DeferredPromise>&&);
    std::unique_ptr<RTCDataChannelHandler> createDataChannel(const String&, const RTCDataChannelInit&);
    void addIceCandidate(std::unique_ptr<webrtc::IceCandidateInterface>&&, PeerConnectionBackend::AddIceCandidateCallback&&);

    void close();
    void stop();
    bool isStopped() const { return !m_backend; }

    bool addTrack(LibWebRTCRtpSenderBackend&, MediaStreamTrack&, const FixedVector<String>&);
    void removeTrack(LibWebRTCRtpSenderBackend&);

    struct Backends {
        std::unique_ptr<LibWebRTCRtpSenderBackend> senderBackend;
        std::unique_ptr<LibWebRTCRtpReceiverBackend> receiverBackend;
        std::unique_ptr<LibWebRTCRtpTransceiverBackend> transceiverBackend;
    };
    ExceptionOr<Backends> addTransceiver(const String& trackKind, const RTCRtpTransceiverInit&, PeerConnectionBackend::IgnoreNegotiationNeededFlag);
    ExceptionOr<Backends> addTransceiver(MediaStreamTrack&, const RTCRtpTransceiverInit&, PeerConnectionBackend::IgnoreNegotiationNeededFlag);
    std::unique_ptr<LibWebRTCRtpTransceiverBackend> transceiverBackendFromSender(LibWebRTCRtpSenderBackend&);

    void setSenderSourceFromTrack(LibWebRTCRtpSenderBackend&, MediaStreamTrack&);
    void collectTransceivers();

    std::optional<bool> canTrickleIceCandidates() const;

    void suspend();
    void resume();
    void disableSocketRelay();

    bool isNegotiationNeeded(uint32_t) const;

    void startRTCLogs();
    void stopRTCLogs();

private:
    LibWebRTCMediaEndpoint(LibWebRTCPeerConnectionBackend&, LibWebRTCProvider&);

    // webrtc::PeerConnectionObserver API
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) final;
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) final;

    void OnNegotiationNeededEvent(uint32_t) final;
    void OnStandardizedIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState) final;
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) final;
    void OnIceCandidate(const webrtc::IceCandidateInterface*) final;
    void OnIceCandidatesRemoved(const std::vector<webrtc::Candidate>&) final;

    void createSessionDescriptionSucceeded(std::unique_ptr<webrtc::SessionDescriptionInterface>&&);
    void createSessionDescriptionFailed(ExceptionCode, const char*);
    void setLocalSessionDescriptionSucceeded();
    void setLocalSessionDescriptionFailed(ExceptionCode, const char*);
    void setRemoteSessionDescriptionSucceeded();
    void setRemoteSessionDescriptionFailed(ExceptionCode, const char*);

    template<typename T>
    ExceptionOr<Backends> createTransceiverBackends(T&&, webrtc::RtpTransceiverInit&&, LibWebRTCRtpSenderBackend::Source&&, PeerConnectionBackend::IgnoreNegotiationNeededFlag);

    void OnStatsDelivered(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&) final;
    void gatherStatsForLogging();
    void startLoggingStats();
    void stopLoggingStats();

    webrtc::scoped_refptr<LibWebRTCStatsCollector> createStatsCollector(Ref<DeferredPromise>&&);

    Vector<Ref<MediaStream>> mediaStreamsFromRTCStreamIds(const Vector<String>&);
    PeerConnectionBackend::TransceiverStates generateTransceiverStates(const Vector<LibWebRTCMediaEndpointTransceiverState>&);

    void AddRef() const { ref(); }
    webrtc::RefCountReleaseStatus Release() const
    {
        auto result = refCount() - 1;
        deref();
        return result ? webrtc::RefCountReleaseStatus::kOtherRefsRemained : webrtc::RefCountReleaseStatus::kDroppedLastRef;
    }

    std::pair<LibWebRTCRtpSenderBackend::Source, Ref<webrtc::MediaStreamTrackInterface>> createSourceAndRTCTrack(MediaStreamTrack&);
    RefPtr<RealtimeMediaSource> sourceFromNewReceiver(webrtc::RtpReceiverInterface&);

#if !RELEASE_LOG_DISABLED
    const Logger& logger() const final { return m_logger.get(); }
    uint64_t logIdentifier() const final { return m_logIdentifier; }
    ASCIILiteral logClassName() const final { return "LibWebRTCMediaEndpoint"_s; }
    WTFLogChannel& logChannel() const final;

    Seconds statsLogInterval(int64_t) const;
#endif

    RefPtr<LibWebRTCPeerConnectionBackend> protectedPeerConnectionBackend() const;

    WeakPtr<LibWebRTCPeerConnectionBackend> m_peerConnectionBackend;
    const Ref<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
    RefPtr<webrtc::PeerConnectionInterface> m_backend;

    friend CreateSessionDescriptionObserver<LibWebRTCMediaEndpoint>;
    friend SetLocalSessionDescriptionObserver<LibWebRTCMediaEndpoint>;
    friend SetRemoteSessionDescriptionObserver<LibWebRTCMediaEndpoint>;

    CreateSessionDescriptionObserver<LibWebRTCMediaEndpoint> m_createSessionDescriptionObserver;
    SetLocalSessionDescriptionObserver<LibWebRTCMediaEndpoint> m_setLocalSessionDescriptionObserver;
    SetRemoteSessionDescriptionObserver<LibWebRTCMediaEndpoint> m_setRemoteSessionDescriptionObserver;

    MemoryCompactRobinHoodHashMap<String, RefPtr<MediaStream>> m_remoteStreamsById;

    bool m_isInitiator { false };
    Timer m_statsLogTimer;

    MemoryCompactRobinHoodHashMap<String, Ref<webrtc::MediaStreamInterface>> m_localStreams;

    const std::unique_ptr<LibWebRTCProvider::SuspendableSocketFactory> m_rtcSocketFactory;
#if !RELEASE_LOG_DISABLED
    int64_t m_statsFirstDeliveredTimestamp { 0 };
    const Ref<const Logger> m_logger;
    const uint64_t m_logIdentifier;
#endif
    bool m_isGatheringRTCLogs { false };
    bool m_shouldIgnoreNegotiationNeededSignal { false };
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(LIBWEBRTC)
