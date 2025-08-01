/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#include "ResponsivenessTimer.h"
#include <wtf/CheckedPtr.h>
#include <wtf/RunLoop.h>
#include <wtf/WeakRef.h>

namespace WebKit {

class WebProcessProxy;

class BackgroundProcessResponsivenessTimer : public CanMakeCheckedPtr<BackgroundProcessResponsivenessTimer> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(BackgroundProcessResponsivenessTimer);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(BackgroundProcessResponsivenessTimer);
public:
    explicit BackgroundProcessResponsivenessTimer(WebProcessProxy&);
    ~BackgroundProcessResponsivenessTimer();
    void updateState();

    void didReceiveBackgroundResponsivenessPong();
    bool isResponsive() const { return m_isResponsive; }

    void invalidate();
    void processTerminated();

private:
    Ref<WebProcessProxy> protectedWebProcessProxy() const;
    void responsivenessCheckTimerFired();
    void timeoutTimerFired();
    void setResponsive(bool);

    bool shouldBeActive() const;
    bool isActive() const;
    void scheduleNextResponsivenessCheck();
    ResponsivenessTimer::Client& client() const;
    Ref<ResponsivenessTimer::Client> protectedClient() const { return client(); }

    WeakRef<WebProcessProxy> m_webProcessProxy;
    Seconds m_checkingInterval;
    RunLoop::Timer m_responsivenessCheckTimer;
    RunLoop::Timer m_timeoutTimer;
    bool m_isResponsive { true };
};

}
