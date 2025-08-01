/*
 * Copyright (C) 2017-2025 Apple Inc. All rights reserved.
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

#include "ActiveDOMObject.h"
#include "CacheStorageConnection.h"
#include "FetchRequest.h"
#include "FetchResponse.h"
#include <wtf/UniqueRef.h>

namespace WebCore {

class ScriptExecutionContext;

class DOMCache final : public RefCounted<DOMCache>, public ActiveDOMObject {
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    static Ref<DOMCache> create(ScriptExecutionContext&, String&&, DOMCacheIdentifier, Ref<CacheStorageConnection>&&);
    ~DOMCache();

    using RequestInfo = FetchRequest::Info;

    using KeysPromise = DOMPromiseDeferred<IDLSequence<IDLInterface<FetchRequest>>>;

    void match(RequestInfo&&, CacheQueryOptions&&, Ref<DeferredPromise>&&);

    using MatchAllPromise = DOMPromiseDeferred<IDLSequence<IDLInterface<FetchResponse>>>;
    void matchAll(std::optional<RequestInfo>&&, CacheQueryOptions&&, MatchAllPromise&&);
    void add(RequestInfo&&, DOMPromiseDeferred<void>&&);

    void addAll(Vector<RequestInfo>&&, DOMPromiseDeferred<void>&&);
    void put(RequestInfo&&, Ref<FetchResponse>&&, DOMPromiseDeferred<void>&&);
    void remove(RequestInfo&&, CacheQueryOptions&&, DOMPromiseDeferred<IDLBoolean>&&);
    void keys(std::optional<RequestInfo>&&, CacheQueryOptions&&, KeysPromise&&);

    const String& name() const { return m_name; }
    DOMCacheIdentifier identifier() const { return m_identifier; }

    using MatchCallback = CompletionHandler<void(ExceptionOr<RefPtr<FetchResponse>>)>;
    void doMatch(RequestInfo&&, CacheQueryOptions&&, MatchCallback&&);

    CacheStorageConnection& connection() { return m_connection.get(); }

private:
    DOMCache(ScriptExecutionContext&, String&& name, DOMCacheIdentifier, Ref<CacheStorageConnection>&&);

    ExceptionOr<Ref<FetchRequest>> requestFromInfo(RequestInfo&&, bool ignoreMethod, bool* requestValidationFailed = nullptr);

    // ActiveDOMObject
    void stop() final;

    void putWithResponseData(DOMPromiseDeferred<void>&&, Ref<FetchRequest>&&, Ref<FetchResponse>&&, ExceptionOr<RefPtr<SharedBuffer>>&&);

    enum class ShouldRetrieveResponses : bool { No, Yes };
    using RecordsCallback = CompletionHandler<void(ExceptionOr<Vector<DOMCacheEngine::Record>>&&)>;
    void queryCache(ResourceRequest&&, const CacheQueryOptions&, ShouldRetrieveResponses, RecordsCallback&&);

    void batchDeleteOperation(const FetchRequest&, CacheQueryOptions&&, CompletionHandler<void(ExceptionOr<bool>&&)>&&);
    void batchPutOperation(const FetchRequest&, FetchResponse&, DOMCacheEngine::ResponseBody&&, CompletionHandler<void(ExceptionOr<void>&&)>&&);
    void batchPutOperation(Vector<DOMCacheEngine::Record>&&, CompletionHandler<void(ExceptionOr<void>&&)>&&);

    Vector<Ref<FetchResponse>> cloneResponses(const Vector<DOMCacheEngine::Record>&, MonotonicTime);
    DOMCacheEngine::Record toConnectionRecord(const FetchRequest&, FetchResponse&, DOMCacheEngine::ResponseBody&&);

    String m_name;
    DOMCacheIdentifier m_identifier;
    const Ref<CacheStorageConnection> m_connection;

    bool m_isStopped { false };
};

} // namespace WebCore
