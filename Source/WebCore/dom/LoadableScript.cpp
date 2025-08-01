/*
 * Copyright (C) 2016 Apple, Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "LoadableScript.h"

#include "CachedResourceLoader.h"
#include "CachedScript.h"
#include "ContentSecurityPolicy.h"
#include "Document.h"
#include "LoadableScriptClient.h"
#include "Settings.h"

namespace WebCore {

LoadableScript::LoadableScript(const AtomString& nonce, ReferrerPolicy policy, RequestPriority fetchPriority, const AtomString& crossOriginMode, const AtomString& charset, const AtomString& initiatorType, bool isInUserAgentShadowTree)
    : ScriptElementCachedScriptFetcher(nonce, policy, fetchPriority, crossOriginMode, charset, initiatorType, isInUserAgentShadowTree)
{
}

LoadableScript::~LoadableScript() = default;

void LoadableScript::addClient(LoadableScriptClient& client)
{
    m_clients.add(client);
    if (isLoaded()) {
        Ref protectedThis { *this };
        client.notifyFinished(*this);
    }
}

void LoadableScript::removeClient(LoadableScriptClient& client)
{
    m_clients.remove(client);
}

void LoadableScript::notifyClientFinished()
{
    Ref protectedThis { *this };
    auto clients = WTF::map(m_clients, [](auto& entry) -> WeakPtr<LoadableScriptClient> {
        return entry.key;
    });
    for (auto& client : clients) {
        if (client)
            client->notifyFinished(*this);
    }
}

}
