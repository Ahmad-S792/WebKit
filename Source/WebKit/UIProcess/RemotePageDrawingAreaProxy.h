/*
 * Copyright (C) 2023-2025 Apple Inc. All rights reserved.
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

#include "DrawingAreaInfo.h"
#include "MessageReceiver.h"
#include "MessageReceiverMap.h"
#include <wtf/TZoneMalloc.h>

namespace WebKit {

class DrawingAreaProxy;
class WebProcessProxy;

class RemotePageDrawingAreaProxy : public IPC::MessageReceiver, public RefCounted<RemotePageDrawingAreaProxy> {
    WTF_MAKE_TZONE_ALLOCATED(RemotePageDrawingAreaProxy);
public:
    static Ref<RemotePageDrawingAreaProxy> create(DrawingAreaProxy&, WebProcessProxy&);

    ~RemotePageDrawingAreaProxy();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    WebProcessProxy& process() { return m_process; }
    DrawingAreaIdentifier identifier() const { return m_identifier; }

private:
    RemotePageDrawingAreaProxy(DrawingAreaProxy&, WebProcessProxy&);

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;

    WeakPtr<DrawingAreaProxy> m_drawingArea;
    DrawingAreaIdentifier m_identifier;
    std::span<IPC::ReceiverName> m_names;
    const Ref<WebProcessProxy> m_process;
};

}
