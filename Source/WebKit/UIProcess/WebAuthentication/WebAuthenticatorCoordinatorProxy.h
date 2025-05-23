/*
 * Copyright (C) 2018-2021 Apple Inc. All rights reserved.
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

#if ENABLE(WEB_AUTHN)

#include "MessageReceiver.h"
#include <WebCore/CredentialRequestOptions.h>
#include <WebCore/FrameIdentifier.h>
#include <WebCore/MediationRequirement.h>
#include <WebCore/PublicKeyCredential.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Forward.h>
#include <wtf/MonotonicTime.h>
#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>

#if HAVE(WEB_AUTHN_AS_MODERN)
OBJC_CLASS _WKASDelegate;
OBJC_CLASS NSArray;
OBJC_CLASS NSString;
OBJC_CLASS ASAuthorization;
OBJC_CLASS ASAuthorizationController;
typedef NSString *ASAuthorizationPublicKeyCredentialUserVerificationPreference;
typedef NSString *ASAuthorizationPublicKeyCredentialAttestationKind;
#endif

namespace WebCore {
enum class AuthenticatorAttachment : uint8_t;
struct ExceptionData;
struct PublicKeyCredentialCreationOptions;
struct AuthenticatorResponseData;
struct PublicKeyCredentialRequestOptions;
class SecurityOriginData;
}

#if HAVE(UNIFIED_ASC_AUTH_UI)
OBJC_CLASS ASCAuthorizationRemotePresenter;
OBJC_CLASS ASCCredentialRequestContext;
OBJC_CLASS ASCAgentProxy;
#endif

namespace WebKit {

class WebPageProxy;

struct FrameInfoData;
struct SharedPreferencesForWebProcess;
struct WebAuthenticationRequestData;

struct AutofillEvent {
    MonotonicTime time;
    String username;
    URL url;
};

using CapabilitiesCompletionHandler = CompletionHandler<void(Vector<KeyValuePair<String, bool>>&&)>;
using RequestCompletionHandler = CompletionHandler<void(const WebCore::AuthenticatorResponseData&, WebCore::AuthenticatorAttachment, const WebCore::ExceptionData&)>;

class WebAuthenticatorCoordinatorProxy : public IPC::MessageReceiver, public RefCounted<WebAuthenticatorCoordinatorProxy> {
    WTF_MAKE_TZONE_ALLOCATED(WebAuthenticatorCoordinatorProxy);
    WTF_MAKE_NONCOPYABLE(WebAuthenticatorCoordinatorProxy);
public:
    static Ref<WebAuthenticatorCoordinatorProxy> create(WebPageProxy&);
    ~WebAuthenticatorCoordinatorProxy();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebProcess() const;

#if HAVE(WEB_AUTHN_AS_MODERN)
    static WeakPtr<WebAuthenticatorCoordinatorProxy>& activeConditionalMediationProxy();
    void pauseConditionalAssertion(CompletionHandler<void()>&&);
    void unpauseConditionalAssertion();
    void makeActiveConditionalAssertion();
    void recordAutofill(const String& username, const URL&);
#endif

private:
    explicit WebAuthenticatorCoordinatorProxy(WebPageProxy&);

    using QueryCompletionHandler = CompletionHandler<void(bool)>;

    // IPC::MessageReceiver.
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) override;

    // Receivers.
    void makeCredential(WebCore::FrameIdentifier, FrameInfoData&&, WebCore::PublicKeyCredentialCreationOptions&&, WebCore::MediationRequirement, RequestCompletionHandler&&);
    void getAssertion(WebCore::FrameIdentifier, FrameInfoData&&, WebCore::PublicKeyCredentialRequestOptions&&, WebCore::MediationRequirement, std::optional<WebCore::SecurityOriginData>, RequestCompletionHandler&&);
    void isUserVerifyingPlatformAuthenticatorAvailable(const WebCore::SecurityOriginData&, QueryCompletionHandler&&);
    void isConditionalMediationAvailable(const WebCore::SecurityOriginData&, QueryCompletionHandler&&);
    void getClientCapabilities(const WebCore::SecurityOriginData&, CapabilitiesCompletionHandler&&);

    void signalUnknownCredential(const WebCore::SecurityOriginData&, WebCore::UnknownCredentialOptions&&, CompletionHandler<void(std::optional<WebCore::ExceptionData>)>&&);
    void signalAllAcceptedCredentials(const WebCore::SecurityOriginData&, WebCore::AllAcceptedCredentialsOptions&&, CompletionHandler<void(std::optional<WebCore::ExceptionData>)>&&);
    void signalCurrentUserDetails(const WebCore::SecurityOriginData&, WebCore::CurrentUserDetailsOptions&&, CompletionHandler<void(std::optional<WebCore::ExceptionData>)>&&);

    void cancel(CompletionHandler<void()>&&);

    void handleRequest(WebAuthenticationRequestData&&, RequestCompletionHandler&&);

    WeakPtr<WebPageProxy> m_webPageProxy;

#if HAVE(UNIFIED_ASC_AUTH_UI) || HAVE(WEB_AUTHN_AS_MODERN)
    bool isASCAvailable();
#endif

#if HAVE(WEB_AUTHN_AS_MODERN)
    RetainPtr<ASAuthorizationController> constructASController(const WebAuthenticationRequestData&);
    RetainPtr<NSArray> requestsForRegistration(const WebCore::PublicKeyCredentialCreationOptions&, const WebCore::SecurityOriginData& callerOrigin);
    RetainPtr<NSArray> requestsForAssertion(const WebCore::PublicKeyCredentialRequestOptions&, const WebCore::SecurityOriginData& callerOrigin, const std::optional<WebCore::SecurityOriginData>& parentOrigin);
    bool removeMatchingAutofillEventForUsername(const String&, const WebCore::SecurityOriginData&);
    void removeExpiredAutofillEvents();

#endif

    void performRequest(WebAuthenticationRequestData&&, RequestCompletionHandler&&);

#if HAVE(UNIFIED_ASC_AUTH_UI)
    RetainPtr<ASCCredentialRequestContext> contextForRequest(WebAuthenticationRequestData&&);
    void performRequestLegacy(RetainPtr<ASCCredentialRequestContext>, RequestCompletionHandler&&);
#endif

#if HAVE(WEB_AUTHN_AS_MODERN)
    RequestCompletionHandler m_completionHandler;
    RetainPtr<_WKASDelegate> m_delegate;
    RetainPtr<ASAuthorizationController> m_controller;
    bool m_paused { false };
    bool m_isConditionalMediation { false };
    Vector<AutofillEvent> m_recentAutofills;
#endif

#if HAVE(UNIFIED_ASC_AUTH_UI)
    RetainPtr<ASCAuthorizationRemotePresenter> m_presenter;
    RetainPtr<ASCAgentProxy> m_proxy;
#endif // HAVE(UNIFIED_ASC_AUTH_UI)
    CompletionHandler<void()> m_cancelHandler;
};

} // namespace WebKit

#endif // ENABLE(WEB_AUTHN)
