/*
 * Copyright (C) 2014-2019 Apple Inc. All rights reserved.
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

#include "config.h"
#include "ProcessAssertion.h"

#include "AuxiliaryProcessProxy.h"
#include "WKBase.h"
#include <wtf/RunLoop.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

ASCIILiteral processAssertionTypeDescription(ProcessAssertionType type)
{
    switch (type) {
    case ProcessAssertionType::NearSuspended:
        return "near-suspended"_s;
    case ProcessAssertionType::Background:
        return "background"_s;
    case ProcessAssertionType::UnboundedNetworking:
        return "unbounded-networking"_s;
    case ProcessAssertionType::Foreground:
        return "foreground"_s;
    case ProcessAssertionType::MediaPlayback:
        return "media-playback"_s;
    case ProcessAssertionType::FinishTaskCanSleep:
        return "finish-task-can-sleep"_s;
    case ProcessAssertionType::FinishTaskInterruptable:
        return "finish-task-interruptible"_s;
    case ProcessAssertionType::BoostedJetsam:
        return "boosted-jetsam"_s;
    }
    return "unknown"_s;
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(ProcessAssertion);
WTF_MAKE_TZONE_ALLOCATED_IMPL(ProcessAndUIAssertion);

void ProcessAssertion::acquireAssertion(Mode mode, CompletionHandler<void()>&& acquisisionHandler)
{
    if (mode == Mode::Async)
        acquireAsync(WTFMove(acquisisionHandler));
    else {
        acquireSync();
        if (acquisisionHandler)
            acquisisionHandler();
    }
}

#if !USE(EXTENSIONKIT)

Ref<ProcessAssertion> ProcessAssertion::create(ProcessID processID, const String& reason, ProcessAssertionType type, Mode mode, const String& environmentIdentifier, CompletionHandler<void()>&& acquisisionHandler)
{
    auto assertion = adoptRef(*new ProcessAssertion(processID, reason, type, environmentIdentifier));
    assertion->acquireAssertion(mode, WTFMove(acquisisionHandler));
    return assertion;
}

Ref<ProcessAssertion> ProcessAssertion::create(AuxiliaryProcessProxy& process, const String& reason, ProcessAssertionType type, Mode mode, CompletionHandler<void()>&& acquisisionHandler)
{
    auto assertion = adoptRef(*new ProcessAssertion(process.processID(), reason, type, process.environmentIdentifier()));
    assertion->acquireAssertion(mode, WTFMove(acquisisionHandler));
    return assertion;
}

Ref<ProcessAndUIAssertion> ProcessAndUIAssertion::create(AuxiliaryProcessProxy& process, const String& reason, ProcessAssertionType type, Mode mode, CompletionHandler<void()>&& acquisisionHandler)
{
    auto assertion = adoptRef(*new ProcessAndUIAssertion(process.processID(), reason, type, process.environmentIdentifier()));
    assertion->acquireAssertion(mode, WTFMove(acquisisionHandler));
    return assertion;
}

Ref<ProcessAndUIAssertion> ProcessAndUIAssertion::create(ProcessID processID, const String& reason, ProcessAssertionType type, const String& environmentIdentifier, Mode mode, CompletionHandler<void()>&& acquisisionHandler)
{
    auto assertion = adoptRef(*new ProcessAndUIAssertion(processID, reason, type, environmentIdentifier));
    assertion->acquireAssertion(mode, WTFMove(acquisisionHandler));
    return assertion;
}

#else

Ref<ProcessAssertion> ProcessAssertion::create(ProcessID processID, const String& reason, ProcessAssertionType type, Mode mode, const String& environmentIdentifier, CompletionHandler<void()>&& acquisisionHandler)
{
    auto assertion = adoptRef(*new ProcessAssertion(processID, reason, type, environmentIdentifier, std::nullopt));
    assertion->acquireAssertion(mode, WTFMove(acquisisionHandler));
    return assertion;
}

Ref<ProcessAssertion> ProcessAssertion::create(AuxiliaryProcessProxy& process, const String& reason, ProcessAssertionType type, Mode mode, CompletionHandler<void()>&& acquisisionHandler)
{
    auto assertion = adoptRef(*new ProcessAssertion(process.processID(), reason, type, process.environmentIdentifier(), process.extensionProcess()));
    assertion->acquireAssertion(mode, WTFMove(acquisisionHandler));
    return assertion;
}

Ref<ProcessAndUIAssertion> ProcessAndUIAssertion::create(AuxiliaryProcessProxy& process, const String& reason, ProcessAssertionType type, Mode mode, CompletionHandler<void()>&& acquisisionHandler)
{
    auto assertion = adoptRef(*new ProcessAndUIAssertion(process.processID(), reason, type, process.environmentIdentifier(), process.extensionProcess()));
    assertion->acquireAssertion(mode, WTFMove(acquisisionHandler));
    return assertion;
}

Ref<ProcessAndUIAssertion> ProcessAndUIAssertion::create(ProcessID processID, const String& reason, ProcessAssertionType type, const String& environmentIdentifier, std::optional<ExtensionProcess>&& extensionProcess, Mode mode, CompletionHandler<void()>&& acquisisionHandler)
{
    auto assertion = adoptRef(*new ProcessAndUIAssertion(processID, reason, type, environmentIdentifier, WTFMove(extensionProcess)));
    assertion->acquireAssertion(mode, WTFMove(acquisisionHandler));
    return assertion;
}

#endif

#if !PLATFORM(COCOA) || !USE(RUNNINGBOARD)

ProcessAssertion::ProcessAssertion(ProcessID pid, const String& reason, ProcessAssertionType assertionType, const String&)
    : m_assertionType(assertionType)
    , m_pid(pid)
    , m_reason(reason)
{
}

ProcessAssertion::~ProcessAssertion() = default;

double ProcessAssertion::remainingRunTimeInSeconds(ProcessID)
{
    return 0;
}

bool ProcessAssertion::isValid() const
{
    return true;
}

void ProcessAssertion::acquireAsync(CompletionHandler<void()>&& completionHandler)
{
    if (completionHandler)
        RunLoop::mainSingleton().dispatch(WTFMove(completionHandler));
}

void ProcessAssertion::acquireSync()
{
}

ProcessAndUIAssertion::ProcessAndUIAssertion(ProcessID pid, const String& reason, ProcessAssertionType assertionType, const String& environmentIdentifier)
    : ProcessAssertion(pid, reason, assertionType, environmentIdentifier)
{
}

ProcessAndUIAssertion::~ProcessAndUIAssertion() = default;

#endif

} // namespace WebKit

