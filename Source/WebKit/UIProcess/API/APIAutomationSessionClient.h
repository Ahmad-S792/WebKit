/*
 * Copyright (C) 2016, 2018 Apple Inc. All rights reserved.
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

#ifndef APIAutomationSessionClient_h
#define APIAutomationSessionClient_h

#include <wtf/CompletionHandler.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/WTFString.h>

namespace WebKit {
class WebAutomationSession;
class WebPageProxy;
}

namespace API {

enum AutomationSessionBrowsingContextOptions : uint16_t {
    AutomationSessionBrowsingContextOptionsPreferNewTab = 1 << 0,
};

#if ENABLE(WK_WEB_EXTENSIONS_IN_WEBDRIVER)
enum AutomationSessionWebExtensionResourceOptions : uint16_t {
    AutomationSessionWebExtensionResourceOptionsPath = 1 << 0,
    AutomationSessionWebExtensionResourceOptionsArchivePath = 1 << 1,
    AutomationSessionWebExtensionResourceOptionsBase64 = 1 << 2,
};
#endif

class AutomationSessionClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(AutomationSessionClient);
public:
    enum class JavaScriptDialogType {
        Alert,
        Confirm,
        Prompt,
        BeforeUnloadConfirm
    };

    enum class BrowsingContextPresentation {
        Tab,
        Window,
    };

#if ENABLE(WK_WEB_EXTENSIONS_IN_WEBDRIVER)
    enum class WebExtensionResourceOptions {
        Path,
        ArchivePath,
        Base64,
    };
#endif

    virtual ~AutomationSessionClient() { }

    virtual WTF::String sessionIdentifier() const { return WTF::String(); }
    virtual void didDisconnectFromRemote(WebKit::WebAutomationSession&) { }
    virtual void requestNewPageWithOptions(WebKit::WebAutomationSession&, AutomationSessionBrowsingContextOptions, CompletionHandler<void(WebKit::WebPageProxy*)>&& completionHandler) { completionHandler(nullptr); }
    virtual void requestMaximizeWindowOfPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&, CompletionHandler<void()>&& completionHandler) { completionHandler(); }
    virtual void requestHideWindowOfPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&, CompletionHandler<void()>&& completionHandler) { completionHandler(); }
    virtual void requestRestoreWindowOfPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&, CompletionHandler<void()>&& completionHandler) { completionHandler(); }
    virtual void requestSwitchToPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&, CompletionHandler<void()>&& completionHandler) { completionHandler(); }
    virtual bool isShowingJavaScriptDialogOnPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&) { return false; }
    virtual void dismissCurrentJavaScriptDialogOnPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&) { }
    virtual void acceptCurrentJavaScriptDialogOnPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&) { }
    virtual std::optional<WTF::String> messageOfCurrentJavaScriptDialogOnPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&) { return std::nullopt; }
    virtual std::optional<WTF::String> defaultTextOfCurrentJavaScriptDialogOnPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&) { return std::nullopt; }
    virtual std::optional<WTF::String> userInputOfCurrentJavaScriptDialogOnPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&) { return std::nullopt; }
    virtual void setUserInputForCurrentJavaScriptPromptOnPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&, const WTF::String&) { }
    virtual std::optional<JavaScriptDialogType> typeOfCurrentJavaScriptDialogOnPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&) { return std::nullopt; }
    virtual BrowsingContextPresentation currentPresentationOfPage(WebKit::WebAutomationSession&, WebKit::WebPageProxy&) { return BrowsingContextPresentation::Window; }
#if ENABLE(WK_WEB_EXTENSIONS_IN_WEBDRIVER)
    virtual void loadWebExtensionWithOptions(WebKit::WebAutomationSession&, API::AutomationSessionWebExtensionResourceOptions, const WTF::String& resource, CompletionHandler<void(const WTF::String&)>&& completionHandler) { completionHandler(WTF::String()); }
    virtual void unloadWebExtension(WebKit::WebAutomationSession&, const WTF::String& identifier, CompletionHandler<void(bool)>&& completionHandler) { completionHandler(false); }
#endif
};

} // namespace API

#endif // APIAutomationSessionClient_h
