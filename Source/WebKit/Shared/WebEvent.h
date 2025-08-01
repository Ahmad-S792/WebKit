/*
 * Copyright (C) 2010-2020 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies)
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

// FIXME: We should probably move to making the WebCore/PlatformFooEvents trivial classes so that
// we can use them as the event type.

#include "WebEventModifier.h"
#include "WebEventType.h"
#include <wtf/CheckedPtr.h>
#include <wtf/OptionSet.h>
#include <wtf/RefCounted.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/UUID.h>
#include <wtf/WallTime.h>
#include <wtf/text/WTFString.h>

namespace IPC {
class Decoder;
class Encoder;
}

namespace WebKit {

class WebEvent : public CanMakeThreadSafeCheckedPtr<WebEvent> {
    WTF_MAKE_TZONE_ALLOCATED(WebEvent);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(WebEvent);
public:
    WebEvent(WebEventType, OptionSet<WebEventModifier>, WallTime timestamp, WTF::UUID authorizationToken);
    WebEvent(WebEventType, OptionSet<WebEventModifier>, WallTime timestamp);

    virtual ~WebEvent() = default;

    WebEventType type() const { return m_type; }

    bool shiftKey() const { return m_modifiers.contains(WebEventModifier::ShiftKey); }
    bool controlKey() const { return m_modifiers.contains(WebEventModifier::ControlKey); }
    bool altKey() const { return m_modifiers.contains(WebEventModifier::AltKey); }
    bool metaKey() const { return m_modifiers.contains(WebEventModifier::MetaKey); }
    bool capsLockKey() const { return m_modifiers.contains(WebEventModifier::CapsLockKey); }

    OptionSet<WebEventModifier> modifiers() const { return m_modifiers; }

    WallTime timestamp() const { return m_timestamp; }

    bool isActivationTriggeringEvent() const;
    WTF::UUID authorizationToken() const { return m_authorizationToken; }

private:
    WebEventType m_type;
    OptionSet<WebEventModifier> m_modifiers;
    WallTime m_timestamp;
    WTF::UUID m_authorizationToken;
};

WTF::TextStream& operator<<(WTF::TextStream&, WebEventType);

} // namespace WebKit
