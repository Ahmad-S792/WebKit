/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(MEDIA_SESSION_COORDINATOR)

#include "MediaSessionCoordinatorProxyPrivate.h"
#include "MessageReceiver.h"
#include <wtf/Forward.h>
#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>

namespace WebCore {
struct ExceptionData;
}

namespace WebKit {

class WebPageProxy;
struct SharedPreferencesForWebProcess;

class RemoteMediaSessionCoordinatorProxy
    : private IPC::MessageReceiver
    , public RefCounted<RemoteMediaSessionCoordinatorProxy>
    , public WebCore::MediaSessionCoordinatorClient {
    WTF_MAKE_TZONE_ALLOCATED(RemoteMediaSessionCoordinatorProxy);
public:
    static Ref<RemoteMediaSessionCoordinatorProxy> create(WebPageProxy&, Ref<MediaSessionCoordinatorProxyPrivate>&&);
    ~RemoteMediaSessionCoordinatorProxy();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebProcess(IPC::Connection&) const;

    void seekTo(double, CompletionHandler<void(bool)>&&);
    void play(CompletionHandler<void(bool)>&&);
    void pause(CompletionHandler<void(bool)>&&);
    void setTrack(const String&, CompletionHandler<void(bool)>&&);

    USING_CAN_MAKE_WEAKPTR(MediaSessionCoordinatorClient);

private:
    explicit RemoteMediaSessionCoordinatorProxy(WebPageProxy&, Ref<MediaSessionCoordinatorProxyPrivate>&&);

    // IPC::MessageReceiver.
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) override;

    // Receivers.
    void join(MediaSessionCommandCompletionHandler&&);
    void leave();
    void coordinateSeekTo(double, MediaSessionCommandCompletionHandler&&);
    void coordinatePlay(MediaSessionCommandCompletionHandler&&);
    void coordinatePause(MediaSessionCommandCompletionHandler&&);
    void coordinateSetTrack(const String&, MediaSessionCommandCompletionHandler&&);
    void positionStateChanged(const std::optional<WebCore::MediaPositionState>&);
    void readyStateChanged(WebCore::MediaSessionReadyState);
    void playbackStateChanged(WebCore::MediaSessionPlaybackState);
    void trackIdentifierChanged(const String&);

    // MediaSessionCoordinatorClient
    void seekSessionToTime(double, CompletionHandler<void(bool)>&&) final;
    void playSession(std::optional<double> atTime, std::optional<MonotonicTime> hostTime, CompletionHandler<void(bool)>&&) final;
    void pauseSession(CompletionHandler<void(bool)>&&) final;
    void setSessionTrack(const String&, CompletionHandler<void(bool)>&&) final;
    void coordinatorStateChanged(WebCore::MediaSessionCoordinatorState) final;

    Ref<WebPageProxy> protectedWebPageProxy();

#if !RELEASE_LOG_DISABLED
    const WTF::Logger& logger() const { return m_logger; }
    uint64_t logIdentifier() const { return m_logIdentifier; }
    ASCIILiteral logClassName() const { return "RemoteMediaSessionCoordinatorProxy"_s; }
    WTFLogChannel& logChannel() const;
#endif

    WeakRef<WebPageProxy> m_webPageProxy;
    const Ref<MediaSessionCoordinatorProxyPrivate> m_privateCoordinator;
#if !RELEASE_LOG_DISABLED
    Ref<const WTF::Logger> m_logger;
    const uint64_t m_logIdentifier;
#endif
};

} // namespace WebKit

#endif // ENABLE(MEDIA_SESSION_COORDINATOR)
