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

#include "config.h"
#include "DOMCacheStorage.h"

#include "CacheQueryOptions.h"
#include "ClientOrigin.h"
#include "EventLoop.h"
#include "JSDOMCache.h"
#include "JSDOMPromiseDeferred.h"
#include "JSFetchResponse.h"
#include "MultiCacheQueryOptions.h"
#include "ScriptExecutionContextInlines.h"
#include "SecurityOrigin.h"
#include <JavaScriptCore/ConsoleTypes.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

Ref<DOMCacheStorage> DOMCacheStorage::create(ScriptExecutionContext& context, Ref<CacheStorageConnection>&& connection)
{
    auto cacheStorage = adoptRef(*new DOMCacheStorage(context, WTFMove(connection)));
    cacheStorage->suspendIfNeeded();
    return cacheStorage;
}

DOMCacheStorage::DOMCacheStorage(ScriptExecutionContext& context, Ref<CacheStorageConnection>&& connection)
    : ActiveDOMObject(&context)
    , m_connection(WTFMove(connection))
{
}

DOMCacheStorage::~DOMCacheStorage() = default;

std::optional<ClientOrigin> DOMCacheStorage::origin() const
{
    RefPtr scriptExecutionContext = this->scriptExecutionContext();
    RefPtr origin = scriptExecutionContext ? scriptExecutionContext->securityOrigin() : nullptr;
    if (!origin)
        return std::nullopt;

    return ClientOrigin { scriptExecutionContext->topOrigin().data(), origin->data() };
}

static void doSequentialMatch(size_t index, Vector<Ref<DOMCache>>&& caches, DOMCache::RequestInfo&& info, CacheQueryOptions&& options, DOMCache::MatchCallback&& completionHandler)
{
    if (index >= caches.size()) {
        completionHandler(nullptr);
        return;
    }

    Ref cache = caches[index];
    cache->doMatch(WTFMove(info), WTFMove(options), [caches = WTFMove(caches), info, options, completionHandler = WTFMove(completionHandler), index](auto&& result) mutable {
        if (result.hasException()) {
            completionHandler(result.releaseException());
            return;
        }
        if (result.returnValue()) {
            completionHandler(result.releaseReturnValue());
            return;
        }
        doSequentialMatch(++index, WTFMove(caches), WTFMove(info), WTFMove(options), WTFMove(completionHandler));
    });
}

static inline void startSequentialMatch(Vector<Ref<DOMCache>>&& caches, DOMCache::RequestInfo&& info, CacheQueryOptions&& options, DOMCache::MatchCallback&& completionHandler)
{
    doSequentialMatch(0, WTFMove(caches), WTFMove(info), WTFMove(options), WTFMove(completionHandler));
}

static inline Ref<DOMCache> copyCache(const Ref<DOMCache>& cache)
{
    return cache.copyRef();
}

void DOMCacheStorage::doSequentialMatch(DOMCache::RequestInfo&& info, CacheQueryOptions&& options, Ref<DeferredPromise>&& promise)
{
    startSequentialMatch(WTF::map(m_caches, copyCache), WTFMove(info), WTFMove(options), [pendingActivity = makePendingActivity(*this), promise = WTFMove(promise)](auto&& result) mutable {
        if (result.hasException()) {
            promise->reject(result.releaseException());
            return;
        }
        if (!result.returnValue()) {
            promise->resolve();
            return;
        }
        promise->resolve<IDLInterface<FetchResponse>>(*result.returnValue());
    });
}

void DOMCacheStorage::match(DOMCache::RequestInfo&& info, MultiCacheQueryOptions&& options, Ref<DeferredPromise>&& promise)
{
    retrieveCaches([this, info = WTFMove(info), options = WTFMove(options), promise = WTFMove(promise)](std::optional<Exception>&& exception) mutable {
        if (exception) {
            queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), exception = WTFMove(exception.value())](auto&) mutable {
                promise->reject(WTFMove(exception));
            });
            return;
        }

        if (!options.cacheName.isNull()) {
            auto position = m_caches.findIf([&](auto& item) { return item->name() == options.cacheName; });
            if (position != notFound) {
                Ref { m_caches[position] }->match(WTFMove(info), WTFMove(options), WTFMove(promise));
                return;
            }
            promise->resolve();
            return;
        }

        this->doSequentialMatch(WTFMove(info), WTFMove(options), WTFMove(promise));
    });
}

void DOMCacheStorage::has(const String& name, DOMPromiseDeferred<IDLBoolean>&& promise)
{
    retrieveCaches([this, name, promise = WTFMove(promise)](std::optional<Exception>&& exception) mutable {
        if (exception) {
            queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), exception = WTFMove(exception.value())](auto&) mutable {
                promise.reject(WTFMove(exception));
            });
            return;
        }
        promise.resolve(m_caches.findIf([&](auto& item) { return item->name() == name; }) != notFound);
    });
}

Ref<DOMCache> DOMCacheStorage::findCacheOrCreate(DOMCacheEngine::CacheInfo&& info, ScriptExecutionContext& context)
{
    auto position = m_caches.findIf([&] (const auto& cache) { return info.identifier == cache->identifier(); });
    if (position != notFound)
        return m_caches[position].copyRef();
    return DOMCache::create(context, WTFMove(info.name), info.identifier, m_connection.copyRef());
}

class ConnectionStorageLock {
    WTF_MAKE_TZONE_ALLOCATED(ConnectionStorageLock);

public:
    ConnectionStorageLock(Ref<CacheStorageConnection>&& connection, const ClientOrigin& origin)
        : m_connection(WTFMove(connection))
        , m_origin(origin)
    {
        m_connection->lockStorage(m_origin);
    }

    ~ConnectionStorageLock()
    {
        m_connection->unlockStorage(m_origin);
    }

private:
    const Ref<CacheStorageConnection> m_connection;
    ClientOrigin m_origin;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(ConnectionStorageLock);

void DOMCacheStorage::retrieveCaches(CompletionHandler<void(std::optional<Exception>&&)>&& callback)
{
    RefPtr scriptExecutionContext = this->scriptExecutionContext();
    auto origin = this->origin();
    if (!origin) {
        callback(convertToExceptionAndLog(scriptExecutionContext.get(), DOMCacheEngine::Error::Stopped));
        return;
    }
    auto retrieveCachesPromise = m_connection->retrieveCaches(*origin, m_updateCounter);
    scriptExecutionContext->enqueueTaskWhenSettled(WTFMove(retrieveCachesPromise), TaskSource::DOMManipulation, [this, callback = WTFMove(callback), pendingActivity = makePendingActivity(*this), connectionStorageLock = makeUnique<ConnectionStorageLock>(m_connection.copyRef(), *origin), context = WTFMove(scriptExecutionContext)] (auto&& result) mutable {
        if (m_isStopped) {
            callback(DOMCacheEngine::convertToException(DOMCacheEngine::Error::Stopped));
            return;
        }
        if (!result) {
            callback(DOMCacheEngine::convertToExceptionAndLog(context.get(), result.error()));
            return;
        }
        if (!context) {
            callback(convertToException(DOMCacheEngine::Error::Stopped));
            return;
        }

        auto& cachesInfo = result.value();

        if (m_updateCounter != cachesInfo.updateCounter) {
            m_updateCounter = cachesInfo.updateCounter;

            m_caches = WTF::map(WTFMove(cachesInfo.infos), [&] (DOMCacheEngine::CacheInfo&& info) {
                return findCacheOrCreate(WTFMove(info), *context);
            });
        }
        callback(std::nullopt);
    }, [] (auto&& callback) {
        callback(makeUnexpected(DOMCacheEngine::Error::Stopped));
    });
}

static void logConsolePersistencyError(ScriptExecutionContext* context, const String& cacheName)
{
    if (!context)
        return;

    context->addConsoleMessage(MessageSource::JS, MessageLevel::Error, makeString("There was an error making "_s, cacheName, " persistent on the filesystem"_s));
}

void DOMCacheStorage::open(const String& name, DOMPromiseDeferred<IDLInterface<DOMCache>>&& promise)
{
    retrieveCaches([this, name, promise = WTFMove(promise)](std::optional<Exception>&& exception) mutable {
        if (exception) {
            queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), exception = WTFMove(exception.value())](auto&) mutable {
                promise.reject(WTFMove(exception));
            });
            return;
        }
        doOpen(name, WTFMove(promise));
    });
}

void DOMCacheStorage::doOpen(const String& name, DOMPromiseDeferred<IDLInterface<DOMCache>>&& promise)
{
    RefPtr context = scriptExecutionContext();
    if (!context) {
        promise.reject(convertToException(DOMCacheEngine::Error::Stopped));
        return;
    }

    auto position = m_caches.findIf([&](auto& item) { return item->name() == name; });
    if (position != notFound) {
        promise.resolve(DOMCache::create(*context, String { m_caches[position]->name() }, m_caches[position]->identifier(), m_connection.copyRef()));
        return;
    }

    auto openPromise = m_connection->open(*origin(), name);
    context->enqueueTaskWhenSettled(WTFMove(openPromise), TaskSource::DOMManipulation, [this, name, promise = WTFMove(promise), pendingActivity = makePendingActivity(*this), connectionStorageLock = makeUnique<ConnectionStorageLock>(m_connection.copyRef(), *origin())] (auto&& result) mutable {
        RefPtr context = scriptExecutionContext();
        if (!result) {
            promise.reject(DOMCacheEngine::convertToExceptionAndLog(context.get(), result.error()));
            return;
        }
        if (!context) {
            promise.reject(convertToException(DOMCacheEngine::Error::Stopped));
            return;
        }
        if (result.value().hadStorageError)
            logConsolePersistencyError(context.get(), name);
        auto cache = DOMCache::create(*context, String { name }, result.value().identifier, m_connection.copyRef());
        promise.resolve(cache);
        m_caches.append(WTFMove(cache));
    });
}

void DOMCacheStorage::remove(const String& name, DOMPromiseDeferred<IDLBoolean>&& promise)
{
    retrieveCaches([this, name, promise = WTFMove(promise)](std::optional<Exception>&& exception) mutable {
        if (exception) {
            queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), exception = WTFMove(exception.value())](auto&) mutable {
                promise.reject(WTFMove(exception));
            });
            return;
        }
        doRemove(name, WTFMove(promise));
    });
}

void DOMCacheStorage::doRemove(const String& name, DOMPromiseDeferred<IDLBoolean>&& promise)
{
    auto position = m_caches.findIf([&](auto& item) { return item->name() == name; });
    if (position == notFound) {
        promise.resolve(false);
        return;
    }

    protectedScriptExecutionContext()->enqueueTaskWhenSettled(m_connection->remove(m_caches[position]->identifier()), TaskSource::DOMManipulation, [this, promise = WTFMove(promise), pendingActivity = makePendingActivity(*this)](const auto& result) mutable {
        if (!result)
            promise.reject(DOMCacheEngine::convertToExceptionAndLog(protectedScriptExecutionContext().get(), result.error()));
        else
            promise.resolve(result.value());
    });
}

void DOMCacheStorage::keys(KeysPromise&& promise)
{
    retrieveCaches([this, promise = WTFMove(promise)](std::optional<Exception>&& exception) mutable {
        if (exception) {
            queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), exception = WTFMove(exception.value())](auto&) mutable {
                promise.reject(WTFMove(exception));
            });
            return;
        }

        promise.resolve(WTF::map(m_caches, [] (const auto& cache) {
            return cache->name();
        }));
    });
}

void DOMCacheStorage::stop()
{
    m_isStopped = true;
}

} // namespace WebCore
