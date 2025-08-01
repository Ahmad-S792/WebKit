/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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

#if ENABLE(WEB_RTC)

#include "RTCNetwork.h"
#include "SharedPreferencesForWebProcess.h"
#include <WebCore/ProcessQualified.h>
#include <WebCore/ScriptExecutionContextIdentifier.h>
#include <wtf/CheckedRef.h>
#include <wtf/Expected.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>

#if PLATFORM(COCOA) && defined __has_include && __has_include(<dns_sd.h>)
#define ENABLE_MDNS 1
#else
#define ENABLE_MDNS 0
#endif

#if ENABLE_MDNS
#include <dns_sd.h>
#endif

namespace IPC {
class Connection;
class Decoder;
}

namespace PAL {
class SessionID;
}

namespace WebCore {
enum class MDNSRegisterError : uint8_t;
}

namespace WebKit {

class NetworkConnectionToWebProcess;

class NetworkMDNSRegister {
public:
    NetworkMDNSRegister(NetworkConnectionToWebProcess&);
    ~NetworkMDNSRegister();

    void ref() const;
    void deref() const;

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);

#if ENABLE_MDNS
    void closeAndForgetService(DNSServiceRef);
#endif

    bool hasRegisteredName(const String&) const;

    std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebProcess() const;

private:
    void unregisterMDNSNames(WebCore::ScriptExecutionContextIdentifier);
    void registerMDNSName(WebCore::ScriptExecutionContextIdentifier, const String& ipAddress, CompletionHandler<void(const String&, std::optional<WebCore::MDNSRegisterError>)>&&);

    PAL::SessionID sessionID() const;

    WeakRef<NetworkConnectionToWebProcess> m_connection;
    HashSet<String> m_registeredNames;

    HashMap<WebCore::ScriptExecutionContextIdentifier, Vector<String>> m_perDocumentRegisteredNames;

#if ENABLE_MDNS
    struct DNSServiceDeallocator;
    HashMap<WebCore::ScriptExecutionContextIdentifier, std::unique_ptr<_DNSServiceRef_t, DNSServiceDeallocator>> m_services;
#endif
};

} // namespace WebKit

#endif // ENABLE(WEB_RTC)
