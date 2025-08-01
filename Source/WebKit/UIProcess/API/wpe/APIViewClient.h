/*
 * Copyright (C) 2017 Igalia S.L.
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

#include "UserMessage.h"
#include <wtf/CompletionHandler.h>
#include <wtf/TZoneMallocInlines.h>

typedef struct OpaqueJSContext* JSGlobalContextRef;

namespace WebKit {
class DownloadProxy;
class WebKitWebResourceLoadManager;
}

namespace WKWPE {
class View;
}

namespace API {

class ViewClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(ViewClient);
public:
    virtual ~ViewClient() = default;

    virtual bool isGLibBasedAPI() { return false; }

    virtual void frameDisplayed(WKWPE::View&) { }
    virtual void willStartLoad(WKWPE::View&) { }
    virtual void didChangePageID(WKWPE::View&) { }
    virtual void didReceiveUserMessage(WKWPE::View&, WebKit::UserMessage&&, CompletionHandler<void(WebKit::UserMessage&&)>&& completionHandler) { completionHandler(WebKit::UserMessage()); }
    virtual WebKit::WebKitWebResourceLoadManager* webResourceLoadManager() { return nullptr; }
    virtual void themeColorDidChange() { }

#if ENABLE(FULLSCREEN_API)
    virtual bool enterFullScreen(WKWPE::View&) { return false; };
    virtual bool exitFullScreen(WKWPE::View&) { return false; };
#endif
};

} // namespace API
