/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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

#if ENABLE(WK_WEB_EXTENSIONS)

#include "APIData.h"
#include "WebExtensionContext.h"
#include "WebExtensionContextIdentifier.h"
#include "WebExtensionTabIdentifier.h"
#include "WebExtensionWindowIdentifier.h"
#include <wtf/URL.h>

namespace WebKit {

struct WebExtensionContextParameters {
    WebExtensionContextIdentifier unprivilegedIdentifier;
    Markable<WebExtensionContextIdentifier> privilegedIdentifier;

    URL baseURL;
    String uniqueIdentifier;
    HashSet<String> unsupportedAPIs;

    HashMap<String, WallTime> grantedPermissions;

    RefPtr<API::Data> localizationJSON;
    RefPtr<API::Data> manifestJSON;

    double manifestVersion { 0 };
    bool isSessionStorageAllowedInContentScripts { false };

    std::optional<WebCore::PageIdentifier> backgroundPageIdentifier;
#if ENABLE(INSPECTOR_EXTENSIONS)
    Vector<WebExtensionContext::PageIdentifierTuple> inspectorPageIdentifiers;
    Vector<WebExtensionContext::PageIdentifierTuple> inspectorBackgroundPageIdentifiers;
#endif
    Vector<WebExtensionContext::PageIdentifierTuple> popupPageIdentifiers;
    Vector<WebExtensionContext::PageIdentifierTuple> tabPageIdentifiers;
};

} // namespace WebKit

#endif // ENABLE(WK_WEB_EXTENSIONS)
