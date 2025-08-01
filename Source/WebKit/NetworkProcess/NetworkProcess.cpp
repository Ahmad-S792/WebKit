/*
 * Copyright (C) 2012-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2018 Sony Interactive Entertainment Inc.
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
#include "NetworkProcess.h"

#include "ArgumentCoders.h"
#include "Attachment.h"
#include "AuthenticationManager.h"
#include "AuxiliaryProcessMessages.h"
#include "BackgroundFetchState.h"
#include "DidFilterKnownLinkDecoration.h"
#include "Download.h"
#include "DownloadProxyMessages.h"
#include "FormDataReference.h"
#include "ITPThirdPartyData.h"
#if ENABLE(LEGACY_CUSTOM_PROTOCOL_MANAGER)
#include "LegacyCustomProtocolManager.h"
#endif
#include "LoadedWebArchive.h"
#include "Logging.h"
#include "NetworkConnectionToWebProcess.h"
#include "NetworkContentRuleListManagerMessages.h"
#include "NetworkLoad.h"
#include "NetworkLoadScheduler.h"
#include "NetworkOriginAccessPatterns.h"
#include "NetworkProcessConnectionParameters.h"
#include "NetworkProcessCreationParameters.h"
#include "NetworkProcessPlatformStrategies.h"
#include "NetworkProcessProxyMessages.h"
#include "NetworkResourceLoader.h"
#include "NetworkSession.h"
#include "NetworkSessionCreationParameters.h"
#include "NetworkStorageManager.h"
#include "PreconnectTask.h"
#include "PrivateClickMeasurementPersistentStore.h"
#include "ProcessAssertion.h"
#include "RTCDataChannelRemoteManagerProxy.h"
#include "RemoteWorkerType.h"
#include "ResourceLoadStatisticsStore.h"
#include "ShouldGrandfatherStatistics.h"
#include "StorageAccessStatus.h"
#include "WebCookieManager.h"
#include "WebPageProxyMessages.h"
#include "WebProcessPoolMessages.h"
#include "WebPushMessage.h"
#include "WebResourceLoadStatisticsStore.h"
#include "WebSharedWorkerServer.h"
#include "WebsiteDataFetchOption.h"
#include "WebsiteDataStore.h"
#include "WebsiteDataStoreParameters.h"
#include "WebsiteDataType.h"
#include <WebCore/ClientOrigin.h>
#include <WebCore/CommonAtomStrings.h>
#include <WebCore/CookieJar.h>
#include <WebCore/CrossOriginPreflightResultCache.h>
#include <WebCore/DNS.h>
#include <WebCore/DeprecatedGlobalSettings.h>
#include <WebCore/DiagnosticLoggingClient.h>
#include <WebCore/HTTPCookieAcceptPolicy.h>
#include <WebCore/LegacySchemeRegistry.h>
#include <WebCore/LogInitialization.h>
#include <WebCore/MIMETypeRegistry.h>
#include <WebCore/NetworkStateNotifier.h>
#include <WebCore/NetworkStorageSession.h>
#include <WebCore/NotificationData.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/SQLiteDatabase.h>
#include <WebCore/SWServer.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/SecurityPolicy.h>
#include <WebCore/UserContentURLPattern.h>
#include <algorithm>
#include <wtf/CallbackAggregator.h>
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/OptionSet.h>
#include <wtf/ProcessPrivilege.h>
#include <wtf/RunLoop.h>
#include <wtf/RuntimeApplicationChecks.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/UUID.h>
#include <wtf/UniqueRef.h>
#include <wtf/WTFProcess.h>
#include <wtf/text/AtomString.h>
#include <wtf/text/MakeString.h>

#if ENABLE(SEC_ITEM_SHIM)
#include "SecItemShim.h"
#endif

#include "NetworkCache.h"
#include "NetworkCacheCoders.h"

#if PLATFORM(COCOA)
#include "CookieStorageUtilsCF.h"
#include "LaunchServicesDatabaseObserver.h"
#include "NetworkSessionCocoa.h"
#include <wtf/cocoa/Entitlements.h>
#endif

#if USE(SOUP)
#include "NetworkSessionSoup.h"
#include <WebCore/SoupNetworkSession.h>
#endif

#if USE(CURL)
#include <WebCore/CurlContext.h>
#endif

namespace WebKit {
using namespace WebCore;

static void callExitSoon(IPC::Connection*)
{
    // If the connection has been closed and we haven't responded in the main thread for 10 seconds
    // the process will exit forcibly.
    auto watchdogDelay = 10_s;

    WorkQueue::create("com.apple.WebKit.NetworkProcess.WatchDogQueue"_s)->dispatchAfter(watchdogDelay, [] {
        // We use _exit here since the watchdog callback is called from another thread and we don't want
        // global destructors or atexit handlers to be called from this thread while the main thread is busy
        // doing its thing.
        RELEASE_LOG_ERROR(IPC, "Exiting process early due to unacknowledged closed-connection");
        terminateProcess(EXIT_FAILURE);
    });
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(NetworkProcess);

NetworkProcess::NetworkProcess(AuxiliaryProcessInitializationParameters&& parameters)
    : m_downloadManager(*this)
#if ENABLE(CONTENT_EXTENSIONS)
    , m_networkContentRuleListManager(*this)
#endif
#if USE(RUNNINGBOARD)
    , m_webSQLiteDatabaseTracker(WebSQLiteDatabaseTracker::create([weakThis = WeakPtr { *this }](bool isHoldingLockedFiles) {
        if (RefPtr protectedThis = weakThis.get())
            protectedThis->setIsHoldingLockedFiles(isHoldingLockedFiles);
    }))
#endif
{
    NetworkProcessPlatformStrategies::initialize();

    addSupplementWithoutRefCountedCheck<AuthenticationManager>();
    addSupplementWithoutRefCountedCheck<WebCookieManager>();
#if ENABLE(LEGACY_CUSTOM_PROTOCOL_MANAGER)
    addSupplementWithoutRefCountedCheck<LegacyCustomProtocolManager>();
#endif
#if HAVE(LSDATABASECONTEXT)
    addSupplement<LaunchServicesDatabaseObserver>();
#endif
#if PLATFORM(COCOA) && ENABLE(LEGACY_CUSTOM_PROTOCOL_MANAGER)
    LegacyCustomProtocolManager::networkProcessCreated(*this);
#endif

    NetworkStateNotifier::singleton().addListener([weakThis = WeakPtr { *this }](bool isOnLine) {
        if (!weakThis)
            return;
        for (auto& webProcessConnection : weakThis->m_webProcessConnections.values())
            webProcessConnection->setOnLineState(isOnLine);
    });

    initialize(WTFMove(parameters));
}

NetworkProcess::~NetworkProcess() = default;

AuthenticationManager& NetworkProcess::authenticationManager()
{
    return *supplement<AuthenticationManager>();
}

Ref<AuthenticationManager> NetworkProcess::protectedAuthenticationManager()
{
    return authenticationManager();
}

DownloadManager& NetworkProcess::downloadManager()
{
    return m_downloadManager;
}

CheckedRef<DownloadManager> NetworkProcess::checkedDownloadManager()
{
    return downloadManager();
}

void NetworkProcess::removeNetworkConnectionToWebProcess(NetworkConnectionToWebProcess& connection)
{
    ASSERT(m_webProcessConnections.contains(connection.webProcessIdentifier()));
    m_webProcessConnections.remove(connection.webProcessIdentifier());
    m_allowedFirstPartiesForCookies.remove(connection.webProcessIdentifier());
    auto completionHandlers = m_webProcessConnectionCloseHandlers.take(connection.webProcessIdentifier());
    for (auto& completionHandler : completionHandlers)
        completionHandler();
}

bool NetworkProcess::shouldTerminate()
{
    // Network process keeps session cookies and credentials, so it should never terminate (as long as UI process connection is alive).
    return false;
}

bool NetworkProcess::dispatchMessage(IPC::Connection& connection, IPC::Decoder& decoder)
{
#if ENABLE(CONTENT_EXTENSIONS)
    if (decoder.messageReceiverName() == Messages::NetworkContentRuleListManager::messageReceiverName()) {
        protectedNetworkContentRuleListManager()->didReceiveMessage(connection, decoder);
        return true;
    }
#endif
    return false;
}

void NetworkProcess::stopRunLoopIfNecessary()
{
    if (m_didSyncCookiesForClose && m_closingStorageManagers.isEmpty())
        stopRunLoop();
}

void NetworkProcess::didClose(IPC::Connection&)
{
    ASSERT(RunLoop::isMain());

    auto callbackAggregator = CallbackAggregator::create([protectedThis = Ref { *this }] {
        ASSERT(RunLoop::isMain());
        protectedThis->m_didSyncCookiesForClose = true;
        protectedThis->stopRunLoopIfNecessary();
    });

    forEachNetworkSession([protectedThis = Ref { *this }, callbackAggregator = WTFMove(callbackAggregator)](auto& session) {
        protectedThis->platformFlushCookies(session.sessionID(), [callbackAggregator] { });
        session.storageManager().syncLocalStorage([callbackAggregator] { });
        session.notifyAdAttributionKitOfSessionTermination();
    });

#if PLATFORM(COCOA)
    if (m_mediaStreamingActivitityToken != NOTIFY_TOKEN_INVALID)
        notify_cancel(m_mediaStreamingActivitityToken);
#endif
}

void NetworkProcess::didCreateDownload()
{
    disableTermination();
}

void NetworkProcess::didDestroyDownload()
{
    enableTermination();
}

IPC::Connection* NetworkProcess::downloadProxyConnection()
{
    return parentProcessConnection();
}

AuthenticationManager& NetworkProcess::downloadsAuthenticationManager()
{
    return authenticationManager();
}

void NetworkProcess::lowMemoryHandler(Critical critical)
{
    if (m_suppressMemoryPressureHandler)
        return;

    WTF::releaseFastMallocFreeMemory();

    forEachNetworkSession([critical](auto& session) {
        session.lowMemoryHandler(critical);
    });
}

void NetworkProcess::initializeNetworkProcess(NetworkProcessCreationParameters&& parameters, CompletionHandler<void()>&& completionHandler)
{
    CompletionHandlerCallingScope callCompletionHandler(WTFMove(completionHandler));

    applyProcessCreationParameters(WTFMove(parameters.auxiliaryProcessParameters));
#if HAVE(SEC_KEY_PROXY)
    WTF::setProcessPrivileges({ ProcessPrivilege::CanAccessRawCookies });
#else
    WTF::setProcessPrivileges({ ProcessPrivilege::CanAccessRawCookies, ProcessPrivilege::CanAccessCredentials });
#endif
    WebCore::SQLiteDatabase::useFastMalloc();
    WebCore::NetworkStorageSession::permitProcessToUseCookieAPI(true);
    platformInitializeNetworkProcess(parameters);

    WTF::Thread::setCurrentThreadIsUserInitiated();
    WebCore::initializeCommonAtomStrings();

    m_suppressMemoryPressureHandler = parameters.shouldSuppressMemoryPressureHandler;
    if (!m_suppressMemoryPressureHandler) {
        Ref memoryPressureHandler = MemoryPressureHandler::singleton();
        memoryPressureHandler->setLowMemoryHandler([weakThis = WeakPtr { *this }] (Critical critical, Synchronous) {
            if (RefPtr process = weakThis.get())
                process->lowMemoryHandler(critical);
        });
        memoryPressureHandler->install();
    }

    setCacheModel(parameters.cacheModel);

    setPrivateClickMeasurementEnabled(parameters.enablePrivateClickMeasurement);
    m_ftpEnabled = parameters.ftpEnabled;

    for (auto [processIdentifier, domain] : parameters.allowedFirstPartiesForCookies)
        addAllowedFirstPartyForCookies(processIdentifier, WTFMove(domain), LoadedWebArchive::No, [] { });

    for (auto& supplement : m_supplements.values())
        supplement->initialize(parameters);

    for (auto& scheme : parameters.urlSchemesRegisteredAsSecure)
        registerURLSchemeAsSecure(scheme);

    for (auto& scheme : parameters.urlSchemesRegisteredAsBypassingContentSecurityPolicy)
        registerURLSchemeAsBypassingContentSecurityPolicy(scheme);

    for (auto& scheme : parameters.urlSchemesRegisteredAsLocal)
        registerURLSchemeAsLocal(scheme);

#if ENABLE(ALL_LEGACY_REGISTERED_SPECIAL_URL_SCHEMES)
    for (auto& scheme : parameters.urlSchemesRegisteredAsNoAccess)
        registerURLSchemeAsNoAccess(scheme);
#endif
    
    for (auto&& websiteDataStoreParameters : WTFMove(parameters.websiteDataStoreParameters))
        addWebsiteDataStore(WTFMove(websiteDataStoreParameters));

    m_localhostAliasesForTesting = WTFMove(parameters.localhostAliasesForTesting);

    updateStorageAccessPromptQuirks(WTFMove(parameters.storageAccessPromptQuirksData));

    if (parameters.defaultRequestTimeoutInterval)
        setDefaultRequestTimeoutInterval(*parameters.defaultRequestTimeoutInterval);

    RELEASE_LOG(Process, "%p - NetworkProcess::initializeNetworkProcess: Presenting processPID=%d", this, legacyPresentingApplicationPID());
}

void NetworkProcess::initializeConnection(IPC::Connection* connection)
{
    AuxiliaryProcess::initializeConnection(connection);

    // We give a chance for didClose() to get called on the main thread but forcefully call _exit() after a delay
    // in case the main thread is unresponsive or didClose() takes too long.
    connection->setDidCloseOnConnectionWorkQueueCallback(callExitSoon);

    for (auto& supplement : m_supplements.values())
        supplement->initializeConnection(connection);
}

void NetworkProcess::createNetworkConnectionToWebProcess(ProcessIdentifier identifier, PAL::SessionID sessionID, NetworkProcessConnectionParameters&& parameters, CompletionHandler<void(std::optional<IPC::Connection::Handle>&&, HTTPCookieAcceptPolicy)>&& completionHandler)
{
    RELEASE_LOG(Process, "%p - NetworkProcess::createNetworkConnectionToWebProcess: Create connection for web process core identifier %" PRIu64, this, identifier.toUInt64());
    auto connectionIdentifiers = IPC::Connection::createConnectionIdentifierPair();
    if (!connectionIdentifiers) {
        completionHandler({ }, HTTPCookieAcceptPolicy::Never);
        return;
    }

    auto newConnection = NetworkConnectionToWebProcess::create(*this, identifier, sessionID, WTFMove(parameters), WTFMove(connectionIdentifiers->server));
    Ref connection = newConnection;

    ASSERT(!m_webProcessConnections.contains(identifier));
    m_webProcessConnections.add(identifier, WTFMove(newConnection));

    CheckedPtr storage = storageSession(sessionID);
    completionHandler(WTFMove(connectionIdentifiers->client), storage ? storage->cookieAcceptPolicy() : HTTPCookieAcceptPolicy::Never);

    connection->setOnLineState(NetworkStateNotifier::singleton().onLine());

#if ENABLE(IPC_TESTING_API)
    if (parameters.ignoreInvalidMessageForTesting)
        connection->connection().setIgnoreInvalidMessageForTesting();
#endif

    for (auto pageID : parameters.pagesWithRelaxedThirdPartyCookieBlocking)
        m_pagesWithRelaxedThirdPartyCookieBlocking.add(pageID);

    if (CheckedPtr session = networkSession(sessionID)) {
        Vector<WebCore::RegistrableDomain> allowedSites;
        auto iter = m_allowedFirstPartiesForCookies.find(identifier);
        if (iter != m_allowedFirstPartiesForCookies.end()) {
            for (auto& site : iter->value.second)
                allowedSites.append(site);
        }
        session->storageManager().startReceivingMessageFromConnection(connection->connection(), allowedSites, connection->sharedPreferencesForWebProcessValue());
    }
}

void NetworkProcess::sharedPreferencesForWebProcessDidChange(WebCore::ProcessIdentifier identifier, SharedPreferencesForWebProcess&& sharedPreferences, CompletionHandler<void()>&& completionHandler)
{
    if (RefPtr connection = m_webProcessConnections.get(identifier))
        connection->updateSharedPreferencesForWebProcess(WTFMove(sharedPreferences));
    completionHandler();
}

void NetworkProcess::addAllowedFirstPartyForCookies(WebCore::ProcessIdentifier processIdentifier, WebCore::RegistrableDomain&& firstPartyForCookies, LoadedWebArchive loadedWebArchive, CompletionHandler<void()>&& completionHandler)
{
    if (!HashSet<WebCore::RegistrableDomain>::isValidValue(firstPartyForCookies))
        return completionHandler();

    auto& pair = m_allowedFirstPartiesForCookies.ensure(processIdentifier, [] {
        return std::make_pair(LoadedWebArchive::No, HashSet<RegistrableDomain> { });
    }).iterator->value;

    auto addResult = pair.second.add(WTFMove(firstPartyForCookies));
    if (addResult.isNewEntry) {
        auto iter = m_webProcessConnections.find(processIdentifier);
        if (iter != m_webProcessConnections.end()) {
            forEachNetworkSession([connection = iter->value->connection().uniqueID(), site = Vector<WebCore::RegistrableDomain> { *addResult.iterator }](auto& session) {
                session.storageManager().addAllowedSitesForConnection(connection, site);
            });
        }
    }

    if (loadedWebArchive == LoadedWebArchive::Yes)
        pair.first = LoadedWebArchive::Yes;

    completionHandler();
}

auto NetworkProcess::allowsFirstPartyForCookies(WebCore::ProcessIdentifier processIdentifier, const URL& firstParty) -> AllowCookieAccess
{
    auto allowCookieAccess = allowsFirstPartyForCookies(processIdentifier, RegistrableDomain { firstParty });
    if (allowCookieAccess == NetworkProcess::AllowCookieAccess::Terminate) {
        // FIXME: This should probably not be necessary. If about:blank is the first party for cookies,
        // we should set it to be the inherited origin then remove this exception.
        if (firstParty.isAboutBlank())
            return AllowCookieAccess::Disallow;

        if (firstParty.isNull())
            return AllowCookieAccess::Disallow; // FIXME: This shouldn't be allowed.
    }

    return allowCookieAccess;
}

auto NetworkProcess::allowsFirstPartyForCookies(WebCore::ProcessIdentifier processIdentifier, const RegistrableDomain& firstPartyDomain) -> AllowCookieAccess
{
    // FIXME: This shouldn't be needed but it is hit sometimes at least with PDFs.
    auto terminateOrDisallow = firstPartyDomain.isEmpty() ? AllowCookieAccess::Disallow : AllowCookieAccess::Terminate;
    if (!decltype(m_allowedFirstPartiesForCookies)::isValidKey(processIdentifier)) {
        ASSERT_NOT_REACHED();
        return terminateOrDisallow;
    }

    auto iterator = m_allowedFirstPartiesForCookies.find(processIdentifier);
    if (iterator == m_allowedFirstPartiesForCookies.end()) {
        ASSERT_NOT_REACHED();
        return terminateOrDisallow;
    }

    if (iterator->value.first == LoadedWebArchive::Yes)
        return AllowCookieAccess::Allow;

    auto& set = iterator->value.second;
    if (!std::remove_reference_t<decltype(set)>::isValidValue(firstPartyDomain)) {
        ASSERT_NOT_REACHED();
        return terminateOrDisallow;
    }

    auto result = set.contains(firstPartyDomain);
    ASSERT(result || terminateOrDisallow == AllowCookieAccess::Disallow);
    return result ? AllowCookieAccess::Allow : terminateOrDisallow;
}

void NetworkProcess::addStorageSession(PAL::SessionID sessionID, const WebsiteDataStoreParameters& parameters)
{
    auto addResult = m_networkStorageSessions.add(sessionID, nullptr);
    if (!addResult.isNewEntry)
        return;

    if (parameters.networkSessionParameters.shouldUseTestingNetworkSession) {
        addResult.iterator->value = newTestingSession(sessionID);
        return;
    }

#if PLATFORM(COCOA)
    RetainPtr<CFHTTPCookieStorageRef> uiProcessCookieStorage;
    if (!sessionID.isEphemeral() && !parameters.uiProcessCookieStorageIdentifier.isEmpty()) {
        SandboxExtension::consumePermanently(parameters.cookieStoragePathExtensionHandle);
        if (sessionID != PAL::SessionID::defaultSessionID())
            uiProcessCookieStorage = cookieStorageFromIdentifyingData(parameters.uiProcessCookieStorageIdentifier);
    }

    auto identifierBase = makeString(uiProcessBundleIdentifier(), '.', sessionID.toUInt64());
    RetainPtr<CFURLStorageSessionRef> storageSession;
    auto cfIdentifier = makeString(identifierBase, ".PrivateBrowsing."_s, WTF::UUID::createVersion4()).createCFString();
    if (sessionID.isEphemeral())
        storageSession = createPrivateStorageSession(cfIdentifier.get(), std::nullopt, WebCore::NetworkStorageSession::ShouldDisableCFURLCache::Yes);
    else if (sessionID != PAL::SessionID::defaultSessionID())
        storageSession = WebCore::NetworkStorageSession::createCFStorageSessionForIdentifier(cfIdentifier.get(), WebCore::NetworkStorageSession::ShouldDisableCFURLCache::Yes);

    if (NetworkStorageSession::processMayUseCookieAPI()) {
        ASSERT(hasProcessPrivilege(ProcessPrivilege::CanAccessRawCookies));
        if (!uiProcessCookieStorage && storageSession)
            uiProcessCookieStorage = adoptCF(_CFURLStorageSessionCopyCookieStorage(kCFAllocatorDefault, storageSession.get()));
    }

    addResult.iterator->value = makeUnique<NetworkStorageSession>(sessionID, WTFMove(storageSession), WTFMove(uiProcessCookieStorage));
#elif USE(CURL)
    if (!parameters.networkSessionParameters.alternativeServiceDirectory.isEmpty())
        SandboxExtension::consumePermanently(parameters.networkSessionParameters.alternativeServiceDirectoryExtensionHandle);

    addResult.iterator->value = makeUnique<NetworkStorageSession>(sessionID, parameters.networkSessionParameters.alternativeServiceDirectory);
#elif USE(SOUP)
    addResult.iterator->value = makeUnique<NetworkStorageSession>(sessionID);
#endif

    CheckedPtr { addResult.iterator->value.get() }->setCookiesVersion(parameters.networkSessionParameters.cookiesVersion);
}

void NetworkProcess::addWebsiteDataStore(WebsiteDataStoreParameters&& parameters)
{
    auto sessionID = parameters.networkSessionParameters.sessionID;
#if PLATFORM(IOS_FAMILY)
    if (auto& handle = parameters.cookieStorageDirectoryExtensionHandle)
        SandboxExtension::consumePermanently(*handle);
    if (auto& handle = parameters.containerCachesDirectoryExtensionHandle)
        SandboxExtension::consumePermanently(*handle);
    if (auto& handle = parameters.parentBundleDirectoryExtensionHandle)
        SandboxExtension::consumePermanently(*handle);
    if (auto& handle = parameters.tempDirectoryExtensionHandle)
        grantAccessToContainerTempDirectory(*handle);
    if (auto& handle = parameters.tempDirectoryRootExtensionHandle)
        SandboxExtension::consumePermanently(*handle);
#endif

    addStorageSession(sessionID, parameters);

#if ENABLE(DECLARATIVE_WEB_PUSH)
    parameters.networkSessionParameters.webPushDaemonConnectionConfiguration.declarativeWebPushEnabled = parameters.networkSessionParameters.isDeclarativeWebPushEnabled;
#endif

    auto& session = m_networkSessions.ensure(sessionID, [&]() {
        return NetworkSession::create(*this, parameters.networkSessionParameters);
    }).iterator->value;

    if (m_isSuspended)
        session->storageManager().suspend([] { });
}

void NetworkProcess::forEachNetworkSession(NOESCAPE const Function<void(NetworkSession&)>& functor)
{
    for (auto& session : m_networkSessions.values())
        functor(*session);
}

std::unique_ptr<WebCore::NetworkStorageSession> NetworkProcess::newTestingSession(PAL::SessionID sessionID)
{
#if PLATFORM(COCOA)
    // Session name should be short enough for shared memory region name to be under the limit, otherwise sandbox rules won't work (see <rdar://problem/13642852>).
    auto session = WebCore::createPrivateStorageSession(makeString("WebKit Test-"_s, getCurrentProcessID()).createCFString().get(), std::nullopt, NetworkStorageSession::ShouldDisableCFURLCache::Yes);
    RetainPtr<CFHTTPCookieStorageRef> cookieStorage;
    if (WebCore::NetworkStorageSession::processMayUseCookieAPI()) {
        ASSERT(hasProcessPrivilege(ProcessPrivilege::CanAccessRawCookies));
        if (session)
            cookieStorage = adoptCF(_CFURLStorageSessionCopyCookieStorage(kCFAllocatorDefault, session.get()));
    }
    return makeUnique<WebCore::NetworkStorageSession>(sessionID, WTFMove(session), WTFMove(cookieStorage));
#elif USE(CURL) || USE(SOUP)
    return makeUnique<WebCore::NetworkStorageSession>(sessionID);
#endif
}

void NetworkProcess::cookieAcceptPolicyChanged(HTTPCookieAcceptPolicy newPolicy)
{
    for (auto& connection : m_webProcessConnections.values())
        connection->cookieAcceptPolicyChanged(newPolicy);
}

WebCore::NetworkStorageSession* NetworkProcess::storageSession(PAL::SessionID sessionID) const
{
    return m_networkStorageSessions.get(sessionID);
}

CheckedPtr<WebCore::NetworkStorageSession> NetworkProcess::checkedStorageSession(PAL::SessionID sessionID) const
{
    return storageSession(sessionID);
}

void NetworkProcess::forEachNetworkStorageSession(NOESCAPE const Function<void(WebCore::NetworkStorageSession&)>& functor)
{
    for (auto& storageSession : m_networkStorageSessions.values())
        functor(*storageSession);
}

NetworkSession* NetworkProcess::networkSession(PAL::SessionID sessionID) const
{
    ASSERT(RunLoop::isMain());
    return m_networkSessions.get(sessionID);
}

CheckedPtr<NetworkSession> NetworkProcess::checkedNetworkSession(PAL::SessionID sessionID) const
{
    return networkSession(sessionID);
}

void NetworkProcess::setSession(PAL::SessionID sessionID, std::unique_ptr<NetworkSession>&& session)
{
    ASSERT(RunLoop::isMain());
    m_networkSessions.set(sessionID, WTFMove(session));
}

void NetworkProcess::destroySession(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    ASSERT(RunLoop::isMain());
#if !USE(SOUP) && !USE(CURL)
    // cURL and Soup based ports destroy the default session right before the process exits to avoid leaking
    // network resources like the cookies database.
    ASSERT(sessionID != PAL::SessionID::defaultSessionID());
#endif

    if (auto session = m_networkSessions.take(sessionID)) {
        auto dataStoreIdentifier = session->dataStoreIdentifier();
        UNUSED_PARAM(dataStoreIdentifier);
        RELEASE_LOG(Storage, "%p - NetworkProcess::destroySession sessionID=%" PRIu64 " identifier=%" PUBLIC_LOG_STRING, this, sessionID.toUInt64(), dataStoreIdentifier ? dataStoreIdentifier->toString().utf8().data() : "null"_s);
        session->invalidateAndCancel();
        Ref storageManager = session->storageManager();
        m_closingStorageManagers.add(storageManager.copyRef());
        storageManager->close([this, protectedThis = Ref { *this }, storageManager, completionHandler = std::exchange(completionHandler, { })]() mutable {
            m_closingStorageManagers.remove(storageManager);
            completionHandler();
            stopRunLoopIfNecessary();
        });
    }
    m_networkStorageSessions.remove(sessionID);
    m_sessionsControlledByAutomation.remove(sessionID);
    if (completionHandler)
        completionHandler();
}

void NetworkProcess::ensureSessionWithDataStoreIdentifierRemoved(WTF::UUID identifier, CompletionHandler<void()>&& completionHandler)
{
    RELEASE_LOG(Storage, "%p - NetworkProcess::ensureSessionWithDataStoreIdentifierRemoved identifier=%" PUBLIC_LOG_STRING, this, identifier.toString().utf8().data());
    for (auto& session : m_networkSessions.values()) {
        if (session->dataStoreIdentifier() == identifier)
            RELEASE_LOG_ERROR(Storage, "NetworkProcess::ensureSessionWithDataStoreIdentifierRemoved session still exists for identifier %" PUBLIC_LOG_STRING, identifier.toString().utf8().data());
    }

    completionHandler();
}

void NetworkProcess::registrableDomainsWithLastAccessedTime(PAL::SessionID sessionID, CompletionHandler<void(std::optional<HashMap<RegistrableDomain, WallTime>>&&)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics()) {
            resourceLoadStatistics->registrableDomainsWithLastAccessedTime(WTFMove(completionHandler));
            return;
        }
    }
    completionHandler(std::nullopt);
}

void NetworkProcess::registrableDomainsExemptFromWebsiteDataDeletion(PAL::SessionID sessionID, CompletionHandler<void(HashSet<RegistrableDomain>)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics()) {
            resourceLoadStatistics->registrableDomainsExemptFromWebsiteDataDeletion(WTFMove(completionHandler));
            return;
        }
    }
    completionHandler({ });
}

void NetworkProcess::dumpResourceLoadStatistics(PAL::SessionID sessionID, CompletionHandler<void(String)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->dumpResourceLoadStatistics(WTFMove(completionHandler));
        else
            completionHandler({ });
    } else {
        ASSERT_NOT_REACHED();
        completionHandler({ });
    }
}

void NetworkProcess::updatePrevalentDomainsToBlockCookiesFor(PAL::SessionID sessionID, const Vector<RegistrableDomain>& domainsToBlock, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr networkStorageSession = storageSession(sessionID))
        networkStorageSession->setPrevalentDomainsToBlockAndDeleteCookiesFor(domainsToBlock);
    completionHandler();
}

void NetworkProcess::isGrandfathered(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->isGrandfathered(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::isPrevalentResource(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->isPrevalentResource(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::isVeryPrevalentResource(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->isVeryPrevalentResource(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::setGrandfathered(PAL::SessionID sessionID, RegistrableDomain&& domain, bool isGrandfathered, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setGrandfathered(WTFMove(domain), isGrandfathered, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setPrevalentResource(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setPrevalentResource(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setPrevalentResourceForDebugMode(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setPrevalentResourceForDebugMode(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setVeryPrevalentResource(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setVeryPrevalentResource(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::clearPrevalentResource(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->clearPrevalentResource(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::scheduleCookieBlockingUpdate(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->scheduleCookieBlockingUpdate(WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::scheduleClearInMemoryAndPersistent(PAL::SessionID sessionID, std::optional<WallTime> modifiedSince, ShouldGrandfatherStatistics shouldGrandfather, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        session->clearIsolatedSessions();
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics()) {
            if (modifiedSince)
                resourceLoadStatistics->scheduleClearInMemoryAndPersistent(modifiedSince.value(), shouldGrandfather, WTFMove(completionHandler));
            else
                resourceLoadStatistics->scheduleClearInMemoryAndPersistent(shouldGrandfather, WTFMove(completionHandler));
        } else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::getResourceLoadStatisticsDataSummary(PAL::SessionID sessionID, CompletionHandler<void(Vector<ITPThirdPartyData>&&)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->aggregatedThirdPartyData(WTFMove(completionHandler));
        else
            completionHandler({ });
    } else {
        ASSERT_NOT_REACHED();
        completionHandler({ });
    }
}

void NetworkProcess::resetParametersToDefaultValues(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        session->resetFirstPartyDNSData();
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->resetParametersToDefaultValues(WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::scheduleStatisticsAndDataRecordsProcessing(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->scheduleStatisticsAndDataRecordsProcessing(WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::statisticsDatabaseHasAllTables(PAL::SessionID sessionID, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->statisticsDatabaseHasAllTables(WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::setResourceLoadStatisticsTimeAdvanceForTesting(PAL::SessionID sessionID, Seconds time, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            return resourceLoadStatistics->setTimeAdvanceForTesting(time, WTFMove(completionHandler));
    }
    completionHandler();
}

void NetworkProcess::setIsRunningResourceLoadStatisticsTest(PAL::SessionID sessionID, bool value, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setIsRunningTest(value, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setSubframeUnderTopFrameDomain(PAL::SessionID sessionID, RegistrableDomain&& subFrameDomain, RegistrableDomain&& topFrameDomain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setSubframeUnderTopFrameDomain(WTFMove(subFrameDomain), WTFMove(topFrameDomain), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::isRegisteredAsRedirectingTo(PAL::SessionID sessionID, RegistrableDomain&& domainRedirectedFrom, RegistrableDomain&& domainRedirectedTo, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->isRegisteredAsRedirectingTo(WTFMove(domainRedirectedFrom), WTFMove(domainRedirectedTo), WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::isRegisteredAsSubFrameUnder(PAL::SessionID sessionID, RegistrableDomain&& subFrameDomain, RegistrableDomain&& topFrameDomain, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->isRegisteredAsSubFrameUnder(WTFMove(subFrameDomain), WTFMove(topFrameDomain), WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::setSubresourceUnderTopFrameDomain(PAL::SessionID sessionID, RegistrableDomain&& subresourceDomain, RegistrableDomain&& topFrameDomain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setSubresourceUnderTopFrameDomain(WTFMove(subresourceDomain), WTFMove(topFrameDomain), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setSubresourceUniqueRedirectTo(PAL::SessionID sessionID, RegistrableDomain&& subresourceDomain, RegistrableDomain&& domainRedirectedTo, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setSubresourceUniqueRedirectTo(WTFMove(subresourceDomain), WTFMove(domainRedirectedTo), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setSubresourceUniqueRedirectFrom(PAL::SessionID sessionID, RegistrableDomain&& subresourceDomain, RegistrableDomain&& domainRedirectedFrom, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setSubresourceUniqueRedirectFrom(WTFMove(subresourceDomain), WTFMove(domainRedirectedFrom), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::isRegisteredAsSubresourceUnder(PAL::SessionID sessionID, RegistrableDomain&& subresourceDomain, RegistrableDomain&& topFrameDomain, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->isRegisteredAsSubresourceUnder(WTFMove(subresourceDomain), WTFMove(topFrameDomain), WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::setTopFrameUniqueRedirectTo(PAL::SessionID sessionID, RegistrableDomain&& topFrameDomain, RegistrableDomain&& domainRedirectedTo, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setTopFrameUniqueRedirectTo(WTFMove(topFrameDomain), WTFMove(domainRedirectedTo), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setTopFrameUniqueRedirectFrom(PAL::SessionID sessionID, RegistrableDomain&& topFrameDomain, RegistrableDomain&& domainRedirectedFrom, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setTopFrameUniqueRedirectFrom(WTFMove(topFrameDomain), WTFMove(domainRedirectedFrom), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}
    
    
void NetworkProcess::setLastSeen(PAL::SessionID sessionID, RegistrableDomain&& domain, Seconds seconds, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setLastSeen(WTFMove(domain), seconds, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::domainIDExistsInDatabase(PAL::SessionID sessionID, int domainID, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->domainIDExistsInDatabase(domainID, WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::mergeStatisticForTesting(PAL::SessionID sessionID, RegistrableDomain&& domain, RegistrableDomain&& topFrameDomain1, RegistrableDomain&& topFrameDomain2, Seconds lastSeen, bool hadUserInteraction, Seconds mostRecentUserInteraction, bool isGrandfathered, bool isPrevalent, bool isVeryPrevalent, uint64_t dataRecordsRemoved, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->mergeStatisticForTesting(WTFMove(domain), WTFMove(topFrameDomain1), WTFMove(topFrameDomain2), lastSeen, hadUserInteraction, mostRecentUserInteraction, isGrandfathered, isPrevalent, isVeryPrevalent, unsigned(dataRecordsRemoved), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::insertExpiredStatisticForTesting(PAL::SessionID sessionID, RegistrableDomain&& domain, uint64_t numberOfOperatingDaysPassed, bool hadUserInteraction, bool isScheduledForAllButCookieDataRemoval, bool isPrevalent, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->insertExpiredStatisticForTesting(WTFMove(domain), unsigned(numberOfOperatingDaysPassed), hadUserInteraction, isScheduledForAllButCookieDataRemoval, isPrevalent, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::getAllStorageAccessEntries(PAL::SessionID sessionID, CompletionHandler<void(Vector<String> domains)>&& completionHandler)
{
    if (CheckedPtr networkStorageSession = storageSession(sessionID))
        completionHandler(networkStorageSession->getAllStorageAccessEntries());
    else {
        ASSERT_NOT_REACHED();
        completionHandler({ });
    }
}

void NetworkProcess::logFrameNavigation(PAL::SessionID sessionID, RegistrableDomain&& targetDomain, RegistrableDomain&& topFrameDomain, RegistrableDomain&& sourceDomain, bool isRedirect, bool isMainFrame, Seconds delayAfterMainFrameDocumentLoad, bool wasPotentiallyInitiatedByUser)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->logFrameNavigation(WTFMove(targetDomain), WTFMove(topFrameDomain), WTFMove(sourceDomain), isRedirect, isMainFrame, delayAfterMainFrameDocumentLoad, wasPotentiallyInitiatedByUser);
    } else
        ASSERT_NOT_REACHED();
}

void NetworkProcess::logUserInteraction(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->logUserInteraction(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::hadUserInteraction(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->hasHadUserInteraction(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::isRelationshipOnlyInDatabaseOnce(PAL::SessionID sessionID, RegistrableDomain&& subDomain, RegistrableDomain&& topDomain, CompletionHandler<void(bool)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->isRelationshipOnlyInDatabaseOnce(WTFMove(subDomain), WTFMove(topDomain), WTFMove(completionHandler));
        else
            completionHandler(false);
    } else {
        ASSERT_NOT_REACHED();
        completionHandler(false);
    }
}

void NetworkProcess::clearUserInteraction(PAL::SessionID sessionID, RegistrableDomain&& domain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->clearUserInteraction(WTFMove(domain), WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::hasLocalStorage(PAL::SessionID sessionID, const RegistrableDomain& domain, CompletionHandler<void(bool)>&& completionHandler)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session)
        return completionHandler(false);

    auto types = OptionSet<WebsiteDataType> { WebsiteDataType::LocalStorage };
    session->storageManager().fetchData(types, NetworkStorageManager::ShouldComputeSize::No, [domain, completionHandler = WTFMove(completionHandler)](auto entries) mutable {
        completionHandler(std::ranges::any_of(entries, [&domain](auto& entry) {
            return domain.matches(entry.origin);
        }));
    });
}

void NetworkProcess::setCacheMaxAgeCapForPrevalentResources(PAL::SessionID sessionID, Seconds seconds, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr networkStorageSession = storageSession(sessionID))
        networkStorageSession->setCacheMaxAgeCapForPrevalentResources(Seconds { seconds });
    else
        ASSERT_NOT_REACHED();
    completionHandler();
}

void NetworkProcess::setGrandfatheringTime(PAL::SessionID sessionID, Seconds seconds, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setGrandfatheringTime(seconds, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setMaxStatisticsEntries(PAL::SessionID sessionID, uint64_t maximumEntryCount, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setMaxStatisticsEntries(maximumEntryCount, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setMinimumTimeBetweenDataRecordsRemoval(PAL::SessionID sessionID, Seconds seconds, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setMinimumTimeBetweenDataRecordsRemoval(seconds, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setPruneEntriesDownTo(PAL::SessionID sessionID, uint64_t pruneTargetCount, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setPruneEntriesDownTo(pruneTargetCount, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setTimeToLiveUserInteraction(PAL::SessionID sessionID, Seconds seconds, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setTimeToLiveUserInteraction(seconds, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setShouldClassifyResourcesBeforeDataRecordsRemoval(PAL::SessionID sessionID, bool value, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setShouldClassifyResourcesBeforeDataRecordsRemoval(value, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setTrackingPreventionEnabled(PAL::SessionID sessionID, bool enabled)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setTrackingPreventionEnabled(enabled);
}

void NetworkProcess::updateStorageAccessPromptQuirks(Vector<WebCore::OrganizationStorageAccessPromptQuirk>&& organizationStorageAccessPromptQuirks)
{
    NetworkStorageSession::updateStorageAccessPromptQuirks(WTFMove(organizationStorageAccessPromptQuirks));
}

void NetworkProcess::setResourceLoadStatisticsLogTestingEvent(bool enabled)
{
    forEachNetworkSession([enabled](auto& session) {
        session.setResourceLoadStatisticsLogTestingEvent(enabled);
    });
}

void NetworkProcess::setResourceLoadStatisticsDebugMode(PAL::SessionID sessionID, bool debugMode, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setResourceLoadStatisticsDebugMode(debugMode, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::isResourceLoadStatisticsEphemeral(PAL::SessionID sessionID, CompletionHandler<void(bool)>&& completionHandler) const
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics()) {
            completionHandler(resourceLoadStatistics->isEphemeral());
            return;
        }
    } else
        ASSERT_NOT_REACHED();
    completionHandler(false);
}

void NetworkProcess::resetCacheMaxAgeCapForPrevalentResources(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr networkStorageSession = storageSession(sessionID))
        networkStorageSession->resetCacheMaxAgeCapForPrevalentResources();
    else
        ASSERT_NOT_REACHED();
    completionHandler();
}

void NetworkProcess::didCommitCrossSiteLoadWithDataTransfer(PAL::SessionID sessionID, RegistrableDomain&& fromDomain, RegistrableDomain&& toDomain, OptionSet<WebCore::CrossSiteNavigationDataTransfer::Flag> navigationDataTransfer, WebPageProxyIdentifier webPageProxyID, WebCore::PageIdentifier webPageID, DidFilterKnownLinkDecoration didFilterKnownLinkDecoration)
{
    ASSERT(!navigationDataTransfer.isEmpty());

    if (CheckedPtr networkStorageSession = storageSession(sessionID)) {
        if (!networkStorageSession->shouldBlockThirdPartyCookies(fromDomain))
            return;

        if (navigationDataTransfer.contains(CrossSiteNavigationDataTransfer::Flag::DestinationLinkDecoration))
            networkStorageSession->didCommitCrossSiteLoadWithDataTransferFromPrevalentResource(toDomain, webPageID);

        if (navigationDataTransfer.contains(CrossSiteNavigationDataTransfer::Flag::ReferrerLinkDecoration))
            protectedParentProcessConnection()->send(Messages::NetworkProcessProxy::DidCommitCrossSiteLoadWithDataTransferFromPrevalentResource(webPageProxyID), 0);
    } else
        ASSERT_NOT_REACHED();

    if (navigationDataTransfer.contains(CrossSiteNavigationDataTransfer::Flag::DestinationLinkDecoration)) {
        if (CheckedPtr session = networkSession(sessionID)) {
            if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
                resourceLoadStatistics->logCrossSiteLoadWithLinkDecoration(WTFMove(fromDomain), WTFMove(toDomain), didFilterKnownLinkDecoration, [] { });
        } else
            ASSERT_NOT_REACHED();
    }
}

void NetworkProcess::setCrossSiteLoadWithLinkDecorationForTesting(PAL::SessionID sessionID, RegistrableDomain&& fromDomain, RegistrableDomain&& toDomain, DidFilterKnownLinkDecoration didFilterKnownLinkDecoration, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics()) {
            resourceLoadStatistics->logCrossSiteLoadWithLinkDecoration(WTFMove(fromDomain), WTFMove(toDomain), didFilterKnownLinkDecoration, WTFMove(completionHandler));
            return;
        }
    } else
        ASSERT_NOT_REACHED();
    completionHandler();
}

void NetworkProcess::resetCrossSiteLoadsWithLinkDecorationForTesting(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr networkStorageSession = storageSession(sessionID))
        networkStorageSession->resetCrossSiteLoadsWithLinkDecorationForTesting();
    else
        ASSERT_NOT_REACHED();
    completionHandler();
}

void NetworkProcess::grantStorageAccessForTesting(PAL::SessionID sessionID, Vector<RegistrableDomain>&& subFrameDomains, RegistrableDomain&& topFrameDomain, CompletionHandler<void(void)>&& completionHandler)
{
    HashSet allowedDomains { "site1.example"_str, "site2.example"_str, "site3.example"_str, "site4.example"_str };
    if (!allowedDomains.contains(topFrameDomain.string())) {
        completionHandler();
        return;
    }
    if (CheckedPtr networkStorageSession = storageSession(sessionID)) {
        for (auto&& subFrameDomain : subFrameDomains)
            networkStorageSession->grantCrossPageStorageAccess(WTFMove(topFrameDomain), WTFMove(subFrameDomain));
    } else
        ASSERT_NOT_REACHED();
    completionHandler();
}

void NetworkProcess::setStorageAccessPermissionForTesting(PAL::SessionID sessionID, bool granted, RegistrableDomain&& topFrameDomain, RegistrableDomain&& subFrameDomain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            return resourceLoadStatistics->setStorageAccessPermissionForTesting(granted, WTFMove(topFrameDomain), WTFMove(subFrameDomain), WTFMove(completionHandler));
    } else
        ASSERT_NOT_REACHED();
    completionHandler();
}

void NetworkProcess::hasIsolatedSession(PAL::SessionID sessionID, const WebCore::RegistrableDomain& domain, CompletionHandler<void(bool)>&& completionHandler) const
{
    bool result = false;
    if (CheckedPtr session = networkSession(sessionID))
        result = session->hasIsolatedSession(domain);
    completionHandler(result);
}

#if ENABLE(APP_BOUND_DOMAINS)
void NetworkProcess::setAppBoundDomainsForResourceLoadStatistics(PAL::SessionID sessionID, HashSet<WebCore::RegistrableDomain>&& appBoundDomains, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics()) {
            resourceLoadStatistics->setAppBoundDomains(WTFMove(appBoundDomains), WTFMove(completionHandler));
            return;
        }
    }
    ASSERT_NOT_REACHED();
    completionHandler();
}
#endif

#if ENABLE(MANAGED_DOMAINS)
void NetworkProcess::setManagedDomainsForResourceLoadStatistics(PAL::SessionID sessionID, HashSet<WebCore::RegistrableDomain>&& managedDomains, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        RefPtr { supplement<WebCookieManager>() }->setHTTPCookieAcceptPolicy(sessionID, WebCore::HTTPCookieAcceptPolicy::AlwaysAccept, [session = WeakPtr { *session }, managedDomains = WTFMove(managedDomains), completionHandler = WTFMove(completionHandler)]() mutable {
            if (session) {
                if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics()) {
                    resourceLoadStatistics->setManagedDomains(WTFMove(managedDomains), WTFMove(completionHandler));
                    return;
                }
            }
            completionHandler();
        });
        return;
    }
    ASSERT_NOT_REACHED();
    completionHandler();
}
#endif

void NetworkProcess::setShouldDowngradeReferrerForTesting(bool enabled, CompletionHandler<void()>&& completionHandler)
{
    forEachNetworkSession([enabled](auto& session) {
        session.setShouldDowngradeReferrerForTesting(enabled);
    });
    completionHandler();
}

void NetworkProcess::setThirdPartyCookieBlockingMode(PAL::SessionID sessionID, WebCore::ThirdPartyCookieBlockingMode blockingMode, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setThirdPartyCookieBlockingMode(blockingMode);
    else
        ASSERT_NOT_REACHED();
    completionHandler();
}

void NetworkProcess::setShouldEnbleSameSiteStrictEnforcementForTesting(PAL::SessionID sessionID, WebCore::SameSiteStrictEnforcementEnabled enabled, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setShouldEnbleSameSiteStrictEnforcement(enabled);
    else
        ASSERT_NOT_REACHED();
    completionHandler();
}

void NetworkProcess::setFirstPartyWebsiteDataRemovalModeForTesting(PAL::SessionID sessionID, WebCore::FirstPartyWebsiteDataRemovalMode mode, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
            resourceLoadStatistics->setFirstPartyWebsiteDataRemovalMode(mode, WTFMove(completionHandler));
        else
            completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setToSameSiteStrictCookiesForTesting(PAL::SessionID sessionID, const WebCore::RegistrableDomain& domain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr networkStorageSession = storageSession(sessionID))
        networkStorageSession->setAllCookiesToSameSiteStrict(domain, WTFMove(completionHandler));
    else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}

void NetworkProcess::setFirstPartyHostCNAMEDomainForTesting(PAL::SessionID sessionID, String&& firstPartyHost, WebCore::RegistrableDomain&& cnameDomain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setFirstPartyHostCNAMEDomain(WTFMove(firstPartyHost), WTFMove(cnameDomain));
    completionHandler();
}

void NetworkProcess::setThirdPartyCNAMEDomainForTesting(PAL::SessionID sessionID, WebCore::RegistrableDomain&& domain, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setThirdPartyCNAMEDomainForTesting(WTFMove(domain));
    completionHandler();
}

void NetworkProcess::setPrivateClickMeasurementEnabled(bool enabled)
{
    m_privateClickMeasurementEnabled = enabled;
}

bool NetworkProcess::privateClickMeasurementEnabled() const
{
    return m_privateClickMeasurementEnabled;
}

void NetworkProcess::notifyMediaStreamingActivity(bool activity)
{
#if PLATFORM(COCOA)
    static constexpr auto notifyMediaStreamingName = "com.apple.WebKit.mediaStreamingActivity"_s;

    if (m_mediaStreamingActivitityToken == NOTIFY_TOKEN_INVALID) {
        auto status = notify_register_check(notifyMediaStreamingName, &m_mediaStreamingActivitityToken);
        if (status != NOTIFY_STATUS_OK || m_mediaStreamingActivitityToken == NOTIFY_TOKEN_INVALID) {
            RELEASE_LOG_ERROR(IPC, "notify_register_check() for %s failed with status (%d) 0x%X", notifyMediaStreamingName.characters(), status, status);
            m_mediaStreamingActivitityToken = NOTIFY_TOKEN_INVALID;
            return;
        }
    }
    auto status = notify_set_state(m_mediaStreamingActivitityToken, activity ? 1 : 0);
    if (status != NOTIFY_STATUS_OK) {
        RELEASE_LOG_ERROR(IPC, "notify_set_state() for %s failed with status (%d) 0x%X", notifyMediaStreamingName.characters(), status, status);
        return;
    }
    status = notify_post(notifyMediaStreamingName);
    RELEASE_LOG_ERROR_IF(status != NOTIFY_STATUS_OK, IPC, "notify_post() for %s failed with status (%d) 0x%X", notifyMediaStreamingName.characters(), status, status);
#else
    UNUSED_PARAM(activity);
#endif
}

void NetworkProcess::setPrivateClickMeasurementDebugMode(PAL::SessionID sessionID, bool enabled)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setPrivateClickMeasurementDebugMode(enabled);
}

void NetworkProcess::setShouldSendPrivateTokenIPCForTesting(PAL::SessionID sessionID, bool enabled) const
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setShouldSendPrivateTokenIPCForTesting(enabled);
}

#if HAVE(ALLOW_ONLY_PARTITIONED_COOKIES)
void NetworkProcess::setOptInCookiePartitioningEnabled(PAL::SessionID sessionID, bool enabled) const
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setOptInCookiePartitioningEnabled(enabled);
}
#endif

void NetworkProcess::preconnectTo(PAL::SessionID sessionID, WebPageProxyIdentifier webPageProxyID, WebCore::PageIdentifier webPageID, WebCore::ResourceRequest&& request, WebCore::StoredCredentialsPolicy storedCredentialsPolicy, std::optional<NavigatingToAppBoundDomain> isNavigatingToAppBoundDomain, uint64_t requiredCookiesVersion)
{
    auto url = request.url();
    auto userAgent = request.httpUserAgent();

    LOG(Network, "(NetworkProcess) Preconnecting to URL %s (storedCredentialsPolicy %i)", url.string().utf8().data(), (int)storedCredentialsPolicy);

#if ENABLE(SERVER_PRECONNECT)
#if ENABLE(LEGACY_CUSTOM_PROTOCOL_MANAGER)
    if (RefPtr { supplement<LegacyCustomProtocolManager>() }->supportsScheme(url.protocol().toString()))
        return;
#endif

    CheckedPtr session = networkSession(sessionID);
    if (!session)
        return;

    NetworkLoadParameters parameters;
    parameters.request = WTFMove(request);
    parameters.webPageProxyID = webPageProxyID;
    parameters.webPageID = webPageID;
    parameters.isNavigatingToAppBoundDomain = isNavigatingToAppBoundDomain;
    parameters.storedCredentialsPolicy = storedCredentialsPolicy;
    parameters.shouldPreconnectOnly = PreconnectOnly::Yes;
    parameters.requiredCookiesVersion = requiredCookiesVersion;

    NetworkLoadParameters parametersForAdditionalPreconnect = parameters;

    session->protectedNetworkLoadScheduler()->startedPreconnectForMainResource(url, userAgent);
    Ref task = PreconnectTask::create(*session, WTFMove(parameters));
    task->start([weakSession = WeakPtr { *session }, url, userAgent, parametersForAdditionalPreconnect = WTFMove(parametersForAdditionalPreconnect)](const WebCore::ResourceError& error, const WebCore::NetworkLoadMetrics& metrics) mutable {
        if (CheckedPtr session = weakSession.get()) {
            session->protectedNetworkLoadScheduler()->finishedPreconnectForMainResource(url, userAgent, error);
#if ENABLE(ADDITIONAL_PRECONNECT_ON_HTTP_1X)
            if (equalLettersIgnoringASCIICase(metrics.protocol, "http/1.1"_s)) {
                auto parameters = parametersForAdditionalPreconnect;
                Ref task = PreconnectTask::create(*session, WTFMove(parameters));
                task->start();
            }
#endif // ENABLE(ADDITIONAL_PRECONNECT_ON_HTTP_1X)
        }
    }, 10_s);
#else
    UNUSED_PARAM(url);
    UNUSED_PARAM(userAgent);
    UNUSED_PARAM(storedCredentialsPolicy);
#endif
}

bool NetworkProcess::sessionIsControlledByAutomation(PAL::SessionID sessionID) const
{
    return m_sessionsControlledByAutomation.contains(sessionID);
}

void NetworkProcess::setSessionIsControlledByAutomation(PAL::SessionID sessionID, bool controlled)
{
    if (controlled)
        m_sessionsControlledByAutomation.add(sessionID);
    else
        m_sessionsControlledByAutomation.remove(sessionID);
}

void NetworkProcess::fetchWebsiteData(PAL::SessionID sessionID, OptionSet<WebsiteDataType> websiteDataTypes, OptionSet<WebsiteDataFetchOption> fetchOptions, CompletionHandler<void(WebsiteData&&)>&& completionHandler)
{
    RELEASE_LOG(Storage, "NetworkProcess::fetchWebsiteData started to fetch data for session %" PRIu64, sessionID.toUInt64());
    struct CallbackAggregator final : public ThreadSafeRefCounted<CallbackAggregator> {
        explicit CallbackAggregator(CompletionHandler<void(WebsiteData&&)>&& completionHandler)
            : m_completionHandler(WTFMove(completionHandler))
        {
        }

        ~CallbackAggregator()
        {
            RunLoop::mainSingleton().dispatch([completionHandler = WTFMove(m_completionHandler), websiteData = WTFMove(m_websiteData)] () mutable {
                completionHandler(WTFMove(websiteData));
                RELEASE_LOG(Storage, "NetworkProcess::fetchWebsiteData finished fetching data");
            });
        }

        CompletionHandler<void(WebsiteData&&)> m_completionHandler;
        WebsiteData m_websiteData;
    };

    auto callbackAggregator = adoptRef(*new CallbackAggregator(WTFMove(completionHandler)));
    CheckedPtr session = networkSession(sessionID);

    if (websiteDataTypes.contains(WebsiteDataType::Cookies)) {
        if (CheckedPtr networkStorageSession = storageSession(sessionID))
            networkStorageSession->getHostnamesWithCookies(callbackAggregator->m_websiteData.hostNamesWithCookies);
    }

    if (websiteDataTypes.contains(WebsiteDataType::Credentials)) {
        if (storageSession(sessionID)) {
            auto securityOrigins = storageSession(sessionID)->credentialStorage().originsWithCredentials();
            for (auto& securityOrigin : securityOrigins)
                callbackAggregator->m_websiteData.entries.append({ securityOrigin, WebsiteDataType::Credentials, 0 });
        }
        if (session) {
            for (auto origin : session->originsWithCredentials())
                callbackAggregator->m_websiteData.entries.append({ origin, WebsiteDataType::Credentials, 0 });
        }
    }

#if PLATFORM(COCOA) || USE(SOUP)
    if (websiteDataTypes.contains(WebsiteDataType::HSTSCache))
        callbackAggregator->m_websiteData.hostNamesWithHSTSCache = hostNamesWithHSTSCache(sessionID);
#endif

    if (websiteDataTypes.contains(WebsiteDataType::ServiceWorkerRegistrations) && session && session->hasServiceWorkerDatabasePath()) {
        session->ensureProtectedSWServer()->getOriginsWithRegistrations([callbackAggregator](const HashSet<SecurityOriginData>& securityOrigins) mutable {
            for (auto& origin : securityOrigins)
                callbackAggregator->m_websiteData.entries.append({ origin, WebsiteDataType::ServiceWorkerRegistrations, 0 });
        });
    }
    if (websiteDataTypes.contains(WebsiteDataType::DiskCache) && session) {
        if (RefPtr cache = session->cache()) {
            cache->fetchData(fetchOptions.contains(WebsiteDataFetchOption::ComputeSizes), [callbackAggregator](auto entries) mutable {
                callbackAggregator->m_websiteData.entries.appendVector(entries);
            });
        }
    }

#if HAVE(ALTERNATIVE_SERVICE)
    if (websiteDataTypes.contains(WebsiteDataType::AlternativeServices) && session) {
        for (auto& origin : session->hostNamesWithAlternativeServices())
            callbackAggregator->m_websiteData.entries.append({ origin, WebsiteDataType::AlternativeServices, 0 });
    }
#endif

    if (websiteDataTypes.contains(WebsiteDataType::ResourceLoadStatistics) && session) {
        if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics()) {
            resourceLoadStatistics->registrableDomains([callbackAggregator](auto&& domains) mutable {
                while (!domains.isEmpty())
                    callbackAggregator->m_websiteData.registrableDomainsWithResourceLoadStatistics.add(domains.takeLast());
            });
        }
    }

    if (NetworkStorageManager::canHandleTypes(websiteDataTypes) && session) {
        auto shouldComputeSize = fetchOptions.contains(WebsiteDataFetchOption::ComputeSizes) ? NetworkStorageManager::ShouldComputeSize::Yes : NetworkStorageManager::ShouldComputeSize::No;
        session->storageManager().fetchData(websiteDataTypes, shouldComputeSize, [callbackAggregator](auto entries) mutable {
            callbackAggregator->m_websiteData.entries.appendVector(WTFMove(entries));
        });
    }
}

void NetworkProcess::performDeleteWebsiteDataTask(TaskIdentifier taskIdentifier, TaskTrigger trigger)
{
    auto task = m_deleteWebsiteDataTasks.take(taskIdentifier);
    if (!task.sessionID)
        return;

    RELEASE_LOG(Storage, "NetworkProcess::performDeleteWebsiteDataTask started task (%" PRIu64 ") because %" PUBLIC_LOG_STRING, taskIdentifier.toUInt64(), trigger == TaskTrigger::Timer ? "timer is fired" : "connections are closed");
    deleteWebsiteDataImpl(*task.sessionID, task.websiteDataTypes, task.modifiedSince, WTFMove(task.completionHandler));
}

void NetworkProcess::deleteWebsiteData(PAL::SessionID sessionID, OptionSet<WebsiteDataType> websiteDataTypes, WallTime modifiedSince, const HashSet<WebCore::ProcessIdentifier>& activeWebProcesses, CompletionHandler<void()>&& completionHandler)
{
    auto taskIdentifier = TaskIdentifier::generate();
    bool willWait = false;
    m_deleteWebsiteDataTasks.add(taskIdentifier, DeleteWebsiteDataTask { sessionID, websiteDataTypes, modifiedSince, WTFMove(completionHandler) });

    RELEASE_LOG(Storage, "NetworkProcess::deleteWebsiteData scheduled task (%" PRIu64 ") to delete data modified since %f for session %" PRIu64, taskIdentifier.toUInt64(), modifiedSince.secondsSinceEpoch().value(), sessionID.toUInt64());
    auto deleteTaskAggregator = WTF::CallbackAggregator::create([weakThis = WeakPtr { *this }, taskIdentifier]() mutable {
        if (RefPtr protectedThis = weakThis.get())
            protectedThis->performDeleteWebsiteDataTask(taskIdentifier);
    });
    for (auto& [identifier, connection] : m_webProcessConnections) {
        if (connection->sessionID() != sessionID || activeWebProcesses.contains(identifier))
            continue;

#if OS(DARWIN)
        Ref ipcConnection = connection->connection();
        auto remoteProcessID = ipcConnection->remoteProcessID();
        // Connection is not available.
        if (!remoteProcessID)
            continue;
        RELEASE_LOG(Storage, "NetworkProcess::deleteWebsiteData task (%" PRIu64 ") will start after process %" PRIu64 " (pid=%d) exits", taskIdentifier.toUInt64(), identifier.toUInt64(), remoteProcessID);
#endif
        auto& completionHandlers = m_webProcessConnectionCloseHandlers.ensure(identifier, [&]() {
            return Vector<CompletionHandler<void()>> { };
        }).iterator->value;
        completionHandlers.append([deleteTaskAggregator] { });
        willWait = true;
    }

    if (websiteDataTypes.contains(WebsiteDataType::ServiceWorkerRegistrations)) {
        CheckedPtr session = networkSession(sessionID);
        RefPtr swServer = session ? session->swServer() : nullptr;
        if (swServer && swServer->addHandlerIfHasControlledClients([deleteTaskAggregator] { }))
            willWait = true;
    }

    if (!willWait)
        return;

    // Schedule a timer in case web processes do not exit on time.
    RunLoop::currentSingleton().dispatchAfter(3_s, [protectedThis = Ref { *this }, taskIdentifier]() mutable {
        protectedThis->performDeleteWebsiteDataTask(taskIdentifier, TaskTrigger::Timer);
    });
}

void NetworkProcess::deleteWebsiteDataImpl(PAL::SessionID sessionID, OptionSet<WebsiteDataType> websiteDataTypes, WallTime modifiedSince, CompletionHandler<void()>&& completionHandler)
{
    auto clearTasksHandler = WTF::CallbackAggregator::create([completionHandler = WTFMove(completionHandler)]() mutable {
        completionHandler();
        RELEASE_LOG(Storage, "NetworkProcess::deleteWebsiteDataImpl finishes deleting modified data");
    });

    RELEASE_LOG(Storage, "NetworkProcess::deleteWebsiteDataImpl starts deleting data modified since %f for session %" PRIu64, modifiedSince.secondsSinceEpoch().value(), sessionID.toUInt64());
    CheckedPtr session = networkSession(sessionID);

#if PLATFORM(COCOA) || USE(SOUP)
    if (websiteDataTypes.contains(WebsiteDataType::HSTSCache))
        clearHSTSCache(sessionID, modifiedSince);
#endif

    if (websiteDataTypes.contains(WebsiteDataType::Cookies)) {
        if (CheckedPtr networkStorageSession = storageSession(sessionID))
            networkStorageSession->deleteAllCookiesModifiedSince(modifiedSince, [clearTasksHandler] { });
    }

    if (websiteDataTypes.contains(WebsiteDataType::Credentials)) {
        if (CheckedPtr storage = storageSession(sessionID))
            storage->credentialStorage().clearCredentials();
        if (session)
            session->clearCredentials(modifiedSince);
    }

    bool clearServiceWorkers = websiteDataTypes.contains(WebsiteDataType::DOMCache) || websiteDataTypes.contains(WebsiteDataType::ServiceWorkerRegistrations);
    if (clearServiceWorkers && !sessionID.isEphemeral() && session) {
        session->ensureProtectedSWServer()->clearAll([clearTasksHandler] { });

#if ENABLE(WEB_PUSH_NOTIFICATIONS)
        session->notificationManager().removeAllPushSubscriptions([clearTasksHandler](auto&&) { });
#endif
    }

    if (websiteDataTypes.contains(WebsiteDataType::ResourceLoadStatistics)) {
        if (RefPtr resourceLoadStatistics = session ? session->resourceLoadStatistics() : nullptr) {
            // If we are deleting all of the data types that the resource load statistics store monitors
            // we do not need to re-grandfather old data.
            auto shouldGrandfather = websiteDataTypes.containsAll(WebResourceLoadStatisticsStore::monitoredDataTypes()) ? ShouldGrandfatherStatistics::No : ShouldGrandfatherStatistics::Yes;
            resourceLoadStatistics->scheduleClearInMemoryAndPersistent(modifiedSince, shouldGrandfather, [clearTasksHandler] { });
        }
    }

    if (session)
        session->removeNetworkWebsiteData(modifiedSince, std::nullopt, [clearTasksHandler] { });

    if (websiteDataTypes.contains(WebsiteDataType::MemoryCache))
        CrossOriginPreflightResultCache::singleton().clear();

    if (websiteDataTypes.contains(WebsiteDataType::DiskCache) && session) {
        if (RefPtr cache = session->cache())
            cache->clear(modifiedSince, [clearTasksHandler] { });
    }

    if (websiteDataTypes.contains(WebsiteDataType::PrivateClickMeasurements) && session)
        session->clearPrivateClickMeasurement([clearTasksHandler] { });

#if HAVE(ALTERNATIVE_SERVICE)
    if (websiteDataTypes.contains(WebsiteDataType::AlternativeServices) && session)
        session->clearAlternativeServices(modifiedSince);
#endif

#if ENABLE(CONTENT_EXTENSIONS)
    if (websiteDataTypes.contains(WebsiteDataType::DiskCache) && session)
        session->clearResourceMonitorThrottlerData([clearTasksHandler] { });
#endif

    if (NetworkStorageManager::canHandleTypes(websiteDataTypes) && session)
        session->storageManager().deleteDataModifiedSince(websiteDataTypes, modifiedSince, [clearTasksHandler] { });
}

void NetworkProcess::deleteWebsiteDataForOrigin(PAL::SessionID sessionID, OptionSet<WebsiteDataType> websiteDataTypes, const ClientOrigin& origin, CompletionHandler<void()>&& completionHandler)
{
    auto clearTasksHandler = WTF::CallbackAggregator::create([completionHandler = WTFMove(completionHandler)]() mutable {
        completionHandler();
        RELEASE_LOG(Storage, "NetworkProcess::deleteWebsiteDataForOrigin finished deleting data");
    });
    RELEASE_LOG(Storage, "NetworkProcess::deleteWebsiteDataForOrigin started to delete data for session %" PRIu64, sessionID.toUInt64());

    CheckedPtr session = networkSession(sessionID);
    if (websiteDataTypes.contains(WebsiteDataType::Cookies)) {
        if (CheckedPtr networkStorageSession = storageSession(sessionID))
            networkStorageSession->deleteCookies(origin, [clearTasksHandler] { });
    }
    if (websiteDataTypes.contains(WebsiteDataType::DiskCache) && !sessionID.isEphemeral()) {
        if (RefPtr cache = session->cache()) {
            Vector<NetworkCache::Key> cacheKeysToDelete;
            String cachePartition = origin.clientOrigin == origin.topOrigin ? emptyString() : ResourceRequest::partitionName(origin.topOrigin.host());
            bool shouldClearAllEntriesInPartition = origin.clientOrigin == origin.topOrigin;
            cache->traverse(cachePartition, [cache, clearTasksHandler, shouldClearAllEntriesInPartition, origin = origin.clientOrigin, cachePartition, cacheKeysToDelete = WTFMove(cacheKeysToDelete)](auto* traversalEntry) mutable {
                if (traversalEntry) {
                    ASSERT_UNUSED(cachePartition, equalIgnoringNullity(traversalEntry->entry.key().partition(), cachePartition));
                    if (shouldClearAllEntriesInPartition || SecurityOriginData::fromURLWithoutStrictOpaqueness(traversalEntry->entry.response().url()) == origin)
                        cacheKeysToDelete.append(traversalEntry->entry.key());
                    return;
                }

                cache->remove(cacheKeysToDelete, [clearTasksHandler] { });
                return;
            });
        }
    }
    if (NetworkStorageManager::canHandleTypes(websiteDataTypes) && session)
        session->storageManager().deleteData(websiteDataTypes, origin, [clearTasksHandler] { });

    bool clearServiceWorkers = websiteDataTypes.contains(WebsiteDataType::DOMCache) || websiteDataTypes.contains(WebsiteDataType::ServiceWorkerRegistrations);
    if (clearServiceWorkers && !sessionID.isEphemeral() && session)
        session->ensureProtectedSWServer()->clear(origin, [clearTasksHandler] { });
}

void NetworkProcess::deleteWebsiteDataForOrigins(PAL::SessionID sessionID, OptionSet<WebsiteDataType> websiteDataTypes, const Vector<SecurityOriginData>& originDatas, const Vector<String>& cookieHostNames, const Vector<String>& HSTSCacheHostNames, const Vector<RegistrableDomain>& registrableDomains, CompletionHandler<void()>&& completionHandler)
{
    auto clearTasksHandler = WTF::CallbackAggregator::create([completionHandler = WTFMove(completionHandler)]() mutable {
        completionHandler();
        RELEASE_LOG(Storage, "NetworkProcess::deleteWebsiteDataForOrigins finished deleting data");
    });

    RELEASE_LOG(Storage, "NetworkProcess::deleteWebsiteDataForOrigins started to delete data for session %" PRIu64, sessionID.toUInt64());
    CheckedPtr session = networkSession(sessionID);

    if (websiteDataTypes.contains(WebsiteDataType::Cookies)) {
        if (CheckedPtr networkStorageSession = storageSession(sessionID))
            networkStorageSession->deleteCookiesForHostnames(cookieHostNames, [clearTasksHandler] { });
    }

#if PLATFORM(COCOA) || USE(SOUP)
    if (websiteDataTypes.contains(WebsiteDataType::HSTSCache))
        deleteHSTSCacheForHostNames(sessionID, HSTSCacheHostNames);
#endif

#if HAVE(ALTERNATIVE_SERVICE)
    if (websiteDataTypes.contains(WebsiteDataType::AlternativeServices) && session) {
        auto hosts = originDatas.map([](auto& originData) {
            return originData.host();
        });
        session->deleteAlternativeServicesForHostNames(hosts);
    }
#endif

    if (websiteDataTypes.contains(WebsiteDataType::PrivateClickMeasurements) && session) {
        for (auto& originData : originDatas)
            session->clearPrivateClickMeasurementForRegistrableDomain(RegistrableDomain::uncheckedCreateFromHost(originData.host()), [clearTasksHandler] { });
    }

    bool clearServiceWorkers = websiteDataTypes.contains(WebsiteDataType::DOMCache) || websiteDataTypes.contains(WebsiteDataType::ServiceWorkerRegistrations);
    if (clearServiceWorkers && !sessionID.isEphemeral() && session) {
        Ref server = session->ensureSWServer();
        for (auto& originData : originDatas) {
            server->clear(originData, [clearTasksHandler] { });

#if ENABLE(WEB_PUSH_NOTIFICATIONS)
            session->notificationManager().removePushSubscriptionsForOrigin(SecurityOriginData { originData }, [clearTasksHandler](auto&&) { });
#endif
        }
    }

    if (websiteDataTypes.contains(WebsiteDataType::MemoryCache))
        CrossOriginPreflightResultCache::singleton().clear();

    if (websiteDataTypes.contains(WebsiteDataType::DiskCache) && session) {
        if (RefPtr cache = session->cache())
            cache->deleteData(originDatas, [clearTasksHandler] { });
    }

    if (websiteDataTypes.contains(WebsiteDataType::Credentials)) {
        if (CheckedPtr storage = storageSession(sessionID)) {
            for (auto& originData : originDatas)
                storage->credentialStorage().removeCredentialsWithOrigin(originData);
        }
        if (session)
            session->removeCredentialsForOrigins(originDatas);
    }

    if (websiteDataTypes.contains(WebsiteDataType::ResourceLoadStatistics) && session) {
        for (auto& domain : registrableDomains) {
            if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
                resourceLoadStatistics->removeDataForDomain(domain, [clearTasksHandler] { });
        }
    }

    if (NetworkStorageManager::canHandleTypes(websiteDataTypes) && session)
        session->storageManager().deleteData(websiteDataTypes, originDatas, [clearTasksHandler] { });

    if (session) {
        HashSet<WebCore::RegistrableDomain> domainsToDeleteNetworkDataFor;
        for (auto& originData : originDatas)
            domainsToDeleteNetworkDataFor.add(WebCore::RegistrableDomain::uncheckedCreateFromHost(originData.host()));
        for (auto& cookieHostName : cookieHostNames)
            domainsToDeleteNetworkDataFor.add(WebCore::RegistrableDomain::uncheckedCreateFromHost(cookieHostName));
        for (auto& cacheHostName : HSTSCacheHostNames)
            domainsToDeleteNetworkDataFor.add(WebCore::RegistrableDomain::uncheckedCreateFromHost(cacheHostName));
        for (auto& domain : registrableDomains)
            domainsToDeleteNetworkDataFor.add(domain);

        session->removeNetworkWebsiteData(std::nullopt, WTFMove(domainsToDeleteNetworkDataFor), [clearTasksHandler] { });
    }
}

static Vector<String> filterForRegistrableDomains(const Vector<RegistrableDomain>& registrableDomains, const HashSet<String>& foundValues)
{
    Vector<String> result;
    for (const auto& value : foundValues) {
        if (registrableDomains.contains(RegistrableDomain::uncheckedCreateFromHost(value)))
            result.append(value);
    }
    
    return result;
}

static Vector<WebCore::SecurityOriginData> filterForRegistrableDomains(const HashSet<WebCore::SecurityOriginData>& origins, const Vector<RegistrableDomain>& domainsToDelete, HashSet<RegistrableDomain>& domainsDeleted)
{
    Vector<SecurityOriginData> originsDeleted;
    for (const auto& origin : origins) {
        auto domain = RegistrableDomain::uncheckedCreateFromHost(origin.host());
        if (!domainsToDelete.contains(domain))
            continue;
        originsDeleted.append(origin);
        domainsDeleted.add(domain);
    }

    return originsDeleted;
}

void NetworkProcess::deleteAndRestrictWebsiteDataForRegistrableDomains(PAL::SessionID sessionID, OptionSet<WebsiteDataType> websiteDataTypes, RegistrableDomainsToDeleteOrRestrictWebsiteDataFor&& domains, CompletionHandler<void(HashSet<RegistrableDomain>&&)>&& completionHandler)
{
    RELEASE_LOG(Storage, "NetworkProcess::deleteAndRestrictWebsiteDataForRegistrableDomains started to delete and restrict data for session %" PRIu64 " with candidate domains - %zu domainsToDeleteAllCookiesFor, %zu domainsToDeleteAllButHttpOnlyCookiesFor, %zu domainsToDeleteAllScriptWrittenStorageFor", sessionID.toUInt64(), domains.domainsToDeleteAllCookiesFor.size(), domains.domainsToDeleteAllButHttpOnlyCookiesFor.size(), domains.domainsToDeleteAllScriptWrittenStorageFor.size());
    CheckedPtr session = networkSession(sessionID);

    OptionSet<WebsiteDataFetchOption> fetchOptions = WebsiteDataFetchOption::DoNotCreateProcesses;

    struct CallbackAggregator final : public ThreadSafeRefCounted<CallbackAggregator> {
        explicit CallbackAggregator(CompletionHandler<void(HashSet<RegistrableDomain>&&)>&& completionHandler)
            : m_completionHandler(WTFMove(completionHandler))
        {
        }
        
        ~CallbackAggregator()
        {
            RunLoop::mainSingleton().dispatch([completionHandler = WTFMove(m_completionHandler), domains = WTFMove(m_domains)] () mutable {
                RELEASE_LOG(Storage, "NetworkProcess::deleteAndRestrictWebsiteDataForRegistrableDomains finished deleting and restricting data");
                completionHandler(WTFMove(domains));
            });
        }
        
        CompletionHandler<void(HashSet<RegistrableDomain>&&)> m_completionHandler;
        HashSet<RegistrableDomain> m_domains;
    };
    
    auto callbackAggregator = adoptRef(*new CallbackAggregator([completionHandler = WTFMove(completionHandler)] (HashSet<RegistrableDomain>&& domainsWithData) mutable {
        RunLoop::mainSingleton().dispatch([completionHandler = WTFMove(completionHandler), domainsWithData = crossThreadCopy(WTFMove(domainsWithData))] () mutable {
            completionHandler(WTFMove(domainsWithData));
        });
    }));

    HashSet<String> hostNamesWithHSTSCache;
    auto domainsToDeleteAllScriptWrittenStorageFor = domains.domainsToDeleteAllScriptWrittenStorageFor;
    if (websiteDataTypes.contains(WebsiteDataType::Cookies)) {
        if (CheckedPtr networkStorageSession = storageSession(sessionID)) {
            HashSet<String> hostNamesWithCookies;
            Vector<String> hostnamesWithCookiesToDelete;
            Vector<String> hostnamesWithCookiesToDeleteAllButHttpOnly;
            Vector<String> hostnamesWithScriptWrittenCookiesToDelete;

            networkStorageSession->getHostnamesWithCookies(hostNamesWithCookies);

            hostnamesWithCookiesToDelete = filterForRegistrableDomains(domains.domainsToDeleteAllCookiesFor, hostNamesWithCookies);
            networkStorageSession->deleteCookiesForHostnames(hostnamesWithCookiesToDelete, WebCore::IncludeHttpOnlyCookies::Yes, ScriptWrittenCookiesOnly::No, [callbackAggregator] { });

#if ENABLE(JS_COOKIE_CHECKING)
            hostnamesWithScriptWrittenCookiesToDelete = filterForRegistrableDomains(domains.domainsToDeleteAllScriptWrittenStorageFor, hostNamesWithCookies);
            networkStorageSession->deleteCookiesForHostnames(hostnamesWithScriptWrittenCookiesToDelete, WebCore::IncludeHttpOnlyCookies::No, ScriptWrittenCookiesOnly::Yes, [callbackAggregator] { });
#endif
            for (const auto& host : hostnamesWithCookiesToDelete)
                callbackAggregator->m_domains.add(RegistrableDomain::uncheckedCreateFromHost(host));

            hostnamesWithCookiesToDeleteAllButHttpOnly = filterForRegistrableDomains(domains.domainsToDeleteAllButHttpOnlyCookiesFor, hostNamesWithCookies);
            networkStorageSession->deleteCookiesForHostnames(hostnamesWithCookiesToDeleteAllButHttpOnly, WebCore::IncludeHttpOnlyCookies::No, ScriptWrittenCookiesOnly::No, [callbackAggregator] { });

            for (const auto& host : hostnamesWithCookiesToDeleteAllButHttpOnly)
                callbackAggregator->m_domains.add(RegistrableDomain::uncheckedCreateFromHost(host));
            RELEASE_LOG(Storage, "NetworkProcess::deleteAndRestrictWebsiteDataForRegistrableDomains deleted cookies for session %" PRIu64 " - %zu domainsToDeleteAllCookiesFor, %zu domainsToDeleteAllButHttpOnlyCookiesFor, %zu domainsToDeleteAllScriptWrittenStorageFor", sessionID.toUInt64(), hostnamesWithCookiesToDelete.size(), hostnamesWithScriptWrittenCookiesToDelete.size(), hostnamesWithCookiesToDeleteAllButHttpOnly.size());
        }
    }

    Vector<String> hostnamesWithHSTSToDelete;
#if PLATFORM(COCOA) || USE(SOUP)
    if (websiteDataTypes.contains(WebsiteDataType::HSTSCache)) {
        hostNamesWithHSTSCache = this->hostNamesWithHSTSCache(sessionID);
        hostnamesWithHSTSToDelete = filterForRegistrableDomains(domainsToDeleteAllScriptWrittenStorageFor, hostNamesWithHSTSCache);

        for (const auto& host : hostnamesWithHSTSToDelete)
            callbackAggregator->m_domains.add(RegistrableDomain::uncheckedCreateFromHost(host));

        deleteHSTSCacheForHostNames(sessionID, hostnamesWithHSTSToDelete);
    }
#endif

#if HAVE(ALTERNATIVE_SERVICE)
    if (websiteDataTypes.contains(WebsiteDataType::AlternativeServices) && session) {
        auto registrableDomainsToDelete = domainsToDeleteAllScriptWrittenStorageFor.map([](auto& domain) {
            return domain.string();
        });
        session->deleteAlternativeServicesForHostNames(registrableDomainsToDelete);
    }
#endif

    if (websiteDataTypes.contains(WebsiteDataType::Credentials)) {
        if (CheckedPtr session = storageSession(sessionID)) {
            auto origins = session->credentialStorage().originsWithCredentials();
            auto originsToDelete = filterForRegistrableDomains(origins, domainsToDeleteAllScriptWrittenStorageFor, callbackAggregator->m_domains);
            for (auto& origin : originsToDelete)
                session->credentialStorage().removeCredentialsWithOrigin(origin);
        }

        if (session) {
            auto origins = session->originsWithCredentials();
            auto originsToDelete = filterForRegistrableDomains(origins, domainsToDeleteAllScriptWrittenStorageFor, callbackAggregator->m_domains);
            session->removeCredentialsForOrigins(originsToDelete);
        }
    }
    
    bool clearServiceWorkers = websiteDataTypes.contains(WebsiteDataType::DOMCache) || websiteDataTypes.contains(WebsiteDataType::ServiceWorkerRegistrations);
    if (clearServiceWorkers && session && session->hasServiceWorkerDatabasePath()) {
        session->ensureProtectedSWServer()->getOriginsWithRegistrations([domainsToDeleteAllScriptWrittenStorageFor, callbackAggregator, session = WeakPtr { *session }](const HashSet<SecurityOriginData>& securityOrigins) mutable {
            for (auto& securityOrigin : securityOrigins) {
                if (!domainsToDeleteAllScriptWrittenStorageFor.contains(RegistrableDomain::uncheckedCreateFromHost(securityOrigin.host())))
                    continue;
                callbackAggregator->m_domains.add(RegistrableDomain::uncheckedCreateFromHost(securityOrigin.host()));
                if (session) {
                    session->ensureProtectedSWServer()->clear(securityOrigin, [callbackAggregator] { });

#if ENABLE(WEB_PUSH_NOTIFICATIONS)
#if ENABLE(DECLARATIVE_WEB_PUSH)
                    if (session->isDeclarativeWebPushEnabled())
                        continue;
#endif
                    session->notificationManager().removePushSubscriptionsForOrigin(SecurityOriginData { securityOrigin }, [callbackAggregator](auto&&) { });
#endif
                }
            }
        });
    }

    if (websiteDataTypes.contains(WebsiteDataType::DiskCache) && session) {
        if (RefPtr cache = session->cache()) {
            cache->deleteDataForRegistrableDomains(domainsToDeleteAllScriptWrittenStorageFor, [callbackAggregator](auto&& deletedDomains) mutable {
                for (auto domain : deletedDomains)
                    callbackAggregator->m_domains.add(WTFMove(domain));
            });
        }
    }

    if (NetworkStorageManager::canHandleTypes(websiteDataTypes) && session) {
        session->storageManager().deleteDataForRegistrableDomains(websiteDataTypes, domainsToDeleteAllScriptWrittenStorageFor, [callbackAggregator](auto&& deletedDomains) mutable {
            for (auto domain : deletedDomains)
                callbackAggregator->m_domains.add(WTFMove(domain));
        });
    }

    auto dataTypesForUIProcess = WebsiteData::filter(websiteDataTypes, WebsiteDataProcessType::UI);
    if (!dataTypesForUIProcess.isEmpty() && !domainsToDeleteAllScriptWrittenStorageFor.isEmpty()) {
        CompletionHandler<void(const HashSet<RegistrableDomain>&)> completionHandler = [callbackAggregator] (const HashSet<RegistrableDomain>& domains) {
            for (auto& domain : domains)
                callbackAggregator->m_domains.add(domain);
        };
        protectedParentProcessConnection()->sendWithAsyncReply(Messages::NetworkProcessProxy::DeleteWebsiteDataInUIProcessForRegistrableDomains(sessionID, dataTypesForUIProcess, fetchOptions, domainsToDeleteAllScriptWrittenStorageFor), WTFMove(completionHandler));
    }
}

void NetworkProcess::deleteCookiesForTesting(PAL::SessionID sessionID, RegistrableDomain domain, bool includeHttpOnlyCookies, CompletionHandler<void()>&& completionHandler)
{
    OptionSet<WebsiteDataType> cookieType = WebsiteDataType::Cookies;
    RegistrableDomainsToDeleteOrRestrictWebsiteDataFor toDeleteFor;
    if (includeHttpOnlyCookies)
        toDeleteFor.domainsToDeleteAllCookiesFor.append(domain);
    else
        toDeleteFor.domainsToDeleteAllButHttpOnlyCookiesFor.append(domain);

    deleteAndRestrictWebsiteDataForRegistrableDomains(sessionID, cookieType, WTFMove(toDeleteFor), [completionHandler = WTFMove(completionHandler)] (HashSet<RegistrableDomain>&& domainsDeletedFor) mutable {
        UNUSED_PARAM(domainsDeletedFor);
        completionHandler();
    });
}

void NetworkProcess::registrableDomainsWithWebsiteData(PAL::SessionID sessionID, OptionSet<WebsiteDataType> websiteDataTypes, CompletionHandler<void(HashSet<RegistrableDomain>&&)>&& completionHandler)
{
    CheckedPtr session = networkSession(sessionID);
    struct CallbackAggregator final : public ThreadSafeRefCounted<CallbackAggregator> {
        explicit CallbackAggregator(CompletionHandler<void(HashSet<RegistrableDomain>&&)>&& completionHandler)
            : m_completionHandler(WTFMove(completionHandler))
        {
        }
        
        ~CallbackAggregator()
        {
            RunLoop::mainSingleton().dispatch([completionHandler = WTFMove(m_completionHandler), websiteData = WTFMove(m_websiteData)] () mutable {
                HashSet<RegistrableDomain> domains;
                for (const auto& hostnameWithCookies : websiteData.hostNamesWithCookies)
                    domains.add(RegistrableDomain::uncheckedCreateFromHost(hostnameWithCookies));

                for (const auto& hostnameWithHSTS : websiteData.hostNamesWithHSTSCache)
                    domains.add(RegistrableDomain::uncheckedCreateFromHost(hostnameWithHSTS));

                for (const auto& entry : websiteData.entries)
                    domains.add(RegistrableDomain::uncheckedCreateFromHost(entry.origin.host()));

                completionHandler(WTFMove(domains));
            });
        }
        
        CompletionHandler<void(HashSet<RegistrableDomain>&&)> m_completionHandler;
        WebsiteData m_websiteData;
    };
    
    auto callbackAggregator = adoptRef(*new CallbackAggregator([completionHandler = WTFMove(completionHandler)] (HashSet<RegistrableDomain>&& domainsWithData) mutable {
        RunLoop::mainSingleton().dispatch([completionHandler = WTFMove(completionHandler), domainsWithData = crossThreadCopy(WTFMove(domainsWithData))] () mutable {
            completionHandler(WTFMove(domainsWithData));
        });
    }));
    
    auto& websiteData = callbackAggregator->m_websiteData;
    
    if (websiteDataTypes.contains(WebsiteDataType::Cookies)) {
        if (CheckedPtr networkStorageSession = storageSession(sessionID))
            networkStorageSession->getHostnamesWithCookies(websiteData.hostNamesWithCookies);
    }
    
#if PLATFORM(COCOA) || USE(SOUP)
    if (websiteDataTypes.contains(WebsiteDataType::HSTSCache))
        websiteData.hostNamesWithHSTSCache = hostNamesWithHSTSCache(sessionID);
#endif

    if (websiteDataTypes.contains(WebsiteDataType::Credentials)) {
        if (CheckedPtr networkStorageSession = storageSession(sessionID)) {
            auto securityOrigins = networkStorageSession->credentialStorage().originsWithCredentials();
            for (auto& securityOrigin : securityOrigins)
                callbackAggregator->m_websiteData.entries.append({ securityOrigin, WebsiteDataType::Credentials, 0 });
        }

        if (session) {
            for (auto origin : session->originsWithCredentials())
                callbackAggregator->m_websiteData.entries.append({ origin, WebsiteDataType::Credentials, 0 });
        }
    }
    
    if (websiteDataTypes.contains(WebsiteDataType::ServiceWorkerRegistrations) && session && session->hasServiceWorkerDatabasePath()) {
        session->ensureProtectedSWServer()->getOriginsWithRegistrations([callbackAggregator](const HashSet<SecurityOriginData>& securityOrigins) mutable {
            for (auto& securityOrigin : securityOrigins)
                callbackAggregator->m_websiteData.entries.append({ securityOrigin, WebsiteDataType::ServiceWorkerRegistrations, 0 });
        });
    }
    
    if (websiteDataTypes.contains(WebsiteDataType::DiskCache) && session) {
        if (RefPtr cache = session->cache()) {
            cache->fetchData(false, [callbackAggregator](auto entries) mutable {
                callbackAggregator->m_websiteData.entries.appendVector(entries);
            });
        }
    }

    if (session) {
        session->storageManager().fetchData(websiteDataTypes, NetworkStorageManager::ShouldComputeSize::No, [callbackAggregator](auto entries) mutable {
            callbackAggregator->m_websiteData.entries.appendVector(WTFMove(entries));
        });
    }
}

void NetworkProcess::closeITPDatabase(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        session->destroyResourceLoadStatistics(WTFMove(completionHandler));
        return;
    }

    completionHandler();
}

void NetworkProcess::downloadRequest(PAL::SessionID sessionID, DownloadID downloadID, const ResourceRequest& request, const std::optional<WebCore::SecurityOriginData>& topOrigin, std::optional<NavigatingToAppBoundDomain> isNavigatingToAppBoundDomain, const String& suggestedFilename)
{
    checkedDownloadManager()->startDownload(sessionID, downloadID, request, topOrigin, isNavigatingToAppBoundDomain, suggestedFilename);
}

void NetworkProcess::resumeDownload(PAL::SessionID sessionID, DownloadID downloadID, std::span<const uint8_t> resumeData, const String& path, WebKit::SandboxExtensionHandle&& sandboxExtensionHandle, CallDownloadDidStart callDownloadDidStart, std::span<const uint8_t> activityAccessToken)
{
    checkedDownloadManager()->resumeDownload(sessionID, downloadID, resumeData, path, WTFMove(sandboxExtensionHandle), callDownloadDidStart, activityAccessToken);
}

void NetworkProcess::cancelDownload(DownloadID downloadID, CompletionHandler<void(std::span<const uint8_t>)>&& completionHandler)
{
    checkedDownloadManager()->cancelDownload(downloadID, WTFMove(completionHandler));
}

#if PLATFORM(COCOA)
#if HAVE(MODERN_DOWNLOADPROGRESS)
void NetworkProcess::publishDownloadProgress(DownloadID downloadID, const URL& url, std::span<const uint8_t> bookmarkData, WebKit::UseDownloadPlaceholder useDownloadPlaceholder, std::span<const uint8_t> activityAccessToken)
{
    downloadManager().publishDownloadProgress(downloadID, url, bookmarkData, useDownloadPlaceholder, activityAccessToken);
}
#else
void NetworkProcess::publishDownloadProgress(DownloadID downloadID, const URL& url, SandboxExtension::Handle&& sandboxExtensionHandle)
{
    checkedDownloadManager()->publishDownloadProgress(downloadID, url, WTFMove(sandboxExtensionHandle));
}
#endif
#endif

void NetworkProcess::findPendingDownloadLocation(NetworkDataTask& networkDataTask, ResponseCompletionHandler&& completionHandler, const ResourceResponse& response)
{
    String suggestedFilename = networkDataTask.suggestedFilename();

    RefPtr { downloadProxyConnection() }->sendWithAsyncReply(Messages::DownloadProxy::DecideDestinationWithSuggestedFilename(response, suggestedFilename), [this, protectedThis = Ref { *this }, completionHandler = WTFMove(completionHandler), networkDataTask = Ref { networkDataTask }] (String&& destination, SandboxExtension::Handle&& sandboxExtensionHandle, AllowOverwrite allowOverwrite, WebKit::UseDownloadPlaceholder usePlaceholder, URL&& alternatePlaceholderURL, SandboxExtension::Handle&& placeholderSandboxExtensionHandle, std::span<const uint8_t> placeholderBookmarkData, std::span<const uint8_t> activityAccessToken) mutable {
#if !HAVE(MODERN_DOWNLOADPROGRESS)
        UNUSED_PARAM(placeholderBookmarkData);
        UNUSED_PARAM(activityAccessToken);
#endif
        auto downloadID = *networkDataTask->pendingDownloadID();
        if (destination.isEmpty())
            return completionHandler(PolicyAction::Ignore);
        networkDataTask->setPendingDownloadLocation(destination, WTFMove(sandboxExtensionHandle), allowOverwrite == AllowOverwrite::Yes);

#if PLATFORM(COCOA)
        URL publishURL;
        if (usePlaceholder == UseDownloadPlaceholder::No && !alternatePlaceholderURL.isEmpty())
            publishURL = alternatePlaceholderURL;
        else
            publishURL = URL::fileURLWithFileSystemPath(destination);
        if (usePlaceholder == UseDownloadPlaceholder::Yes || !alternatePlaceholderURL.isEmpty())
#if HAVE(MODERN_DOWNLOADPROGRESS)
            publishDownloadProgress(downloadID, publishURL, placeholderBookmarkData, usePlaceholder, activityAccessToken);
#else
            publishDownloadProgress(downloadID, publishURL, WTFMove(placeholderSandboxExtensionHandle));
#endif // HAVE(MODERN_DOWNLOADPROGRESS)
#endif // PLATFORM(COCOA)

        completionHandler(PolicyAction::Download);
        if (networkDataTask->state() == NetworkDataTask::State::Canceling || networkDataTask->state() == NetworkDataTask::State::Completed)
            return;

        CheckedRef downloadManager = this->downloadManager();

        if (downloadManager->download(downloadID)) {
            // The completion handler already called dataTaskBecameDownloadTask().
            return;
        }

        downloadManager->downloadDestinationDecided(downloadID, WTFMove(networkDataTask));
    }, *networkDataTask.pendingDownloadID());
}

void NetworkProcess::dataTaskWithRequest(WebPageProxyIdentifier pageID, PAL::SessionID sessionID, WebCore::ResourceRequest&& request, const std::optional<WebCore::SecurityOriginData>& topOrigin, IPC::FormDataReference&& httpBody, CompletionHandler<void(std::optional<DataTaskIdentifier>)>&& completionHandler)
{
    request.setHTTPBody(httpBody.takeData());
    checkedNetworkSession(sessionID)->dataTaskWithRequest(pageID, WTFMove(request), topOrigin, [completionHandler = WTFMove(completionHandler)](auto dataTaskIdentifier) mutable {
        completionHandler(dataTaskIdentifier);
    });
}

void NetworkProcess::cancelDataTask(DataTaskIdentifier identifier, PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->cancelDataTask(identifier);
    completionHandler();
}

void NetworkProcess::setCacheModelSynchronouslyForTesting(CacheModel cacheModel, CompletionHandler<void()>&& completionHandler)
{
    setCacheModel(cacheModel);
    completionHandler();
}

void NetworkProcess::setCacheModel(CacheModel cacheModel)
{
    if (m_hasSetCacheModel && (cacheModel == m_cacheModel))
        return;

    m_hasSetCacheModel = true;
    m_cacheModel = cacheModel;

    forEachNetworkSession([](auto& session) {
        if (RefPtr cache = session.cache())
            cache->updateCapacity();
    });
}

void NetworkProcess::allowTLSCertificateChainForLocalPCMTesting(PAL::SessionID sessionID, const WebCore::CertificateInfo& certificateInfo)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->allowTLSCertificateChainForLocalPCMTesting(certificateInfo);
}

void NetworkProcess::logDiagnosticMessage(WebPageProxyIdentifier webPageProxyID, const String& message, const String& description, ShouldSample shouldSample)
{
    if (!DiagnosticLoggingClient::shouldLogAfterSampling(shouldSample))
        return;

    protectedParentProcessConnection()->send(Messages::NetworkProcessProxy::LogDiagnosticMessage(webPageProxyID, message, description, ShouldSample::No), 0);
}

void NetworkProcess::logDiagnosticMessageWithResult(WebPageProxyIdentifier webPageProxyID, const String& message, const String& description, DiagnosticLoggingResultType result, ShouldSample shouldSample)
{
    if (!DiagnosticLoggingClient::shouldLogAfterSampling(shouldSample))
        return;

    protectedParentProcessConnection()->send(Messages::NetworkProcessProxy::LogDiagnosticMessageWithResult(webPageProxyID, message, description, result, ShouldSample::No), 0);
}

void NetworkProcess::logDiagnosticMessageWithValue(WebPageProxyIdentifier webPageProxyID, const String& message, const String& description, double value, unsigned significantFigures, ShouldSample shouldSample)
{
    if (!DiagnosticLoggingClient::shouldLogAfterSampling(shouldSample))
        return;

    protectedParentProcessConnection()->send(Messages::NetworkProcessProxy::LogDiagnosticMessageWithValue(webPageProxyID, message, description, value, significantFigures, ShouldSample::No), 0);
}

void NetworkProcess::terminate()
{
    platformTerminate();
    AuxiliaryProcess::terminate();
}

void NetworkProcess::processWillSuspendImminentlyForTestingSync(CompletionHandler<void()>&& completionHandler)
{
    prepareToSuspend(true, MonotonicTime::now(), WTFMove(completionHandler));
}

void NetworkProcess::terminateRemoteWorkerContextConnectionWhenPossible(RemoteWorkerType workerType, PAL::SessionID sessionID, const WebCore::RegistrableDomain& registrableDomain, WebCore::ProcessIdentifier processIdentifier)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session)
        return;

    switch (workerType) {
    case RemoteWorkerType::ServiceWorker:
        if (RefPtr swServer = session->swServer())
            swServer->terminateContextConnectionWhenPossible(registrableDomain, processIdentifier);
        break;
    case RemoteWorkerType::SharedWorker:
        if (CheckedPtr sharedWorkerServer = session->sharedWorkerServer())
            sharedWorkerServer->terminateContextConnectionWhenPossible(registrableDomain, processIdentifier);
        break;
    }
}

void NetworkProcess::runningOrTerminatingServiceWorkerCountForTesting(PAL::SessionID sessionID, CompletionHandler<void(unsigned)>&& completionHandler) const
{
    CheckedPtr session = networkSession(sessionID);
    if (!session)
        return completionHandler(0);

    completionHandler(session->ensureSWServer().runningOrTerminatingCount());
}

void NetworkProcess::prepareToSuspend(bool isSuspensionImminent, MonotonicTime estimatedSuspendTime, CompletionHandler<void()>&& completionHandler)
{
#if !RELEASE_LOG_DISABLED
    auto nowTime = MonotonicTime::now();
    double remainingRunTime = estimatedSuspendTime > nowTime ? (estimatedSuspendTime - nowTime).value() : 0.0;
#endif
    RELEASE_LOG(ProcessSuspension, "%p - NetworkProcess::prepareToSuspend(), isSuspensionImminent=%d, remainingRunTime=%fs", this, isSuspensionImminent, remainingRunTime);

    m_isSuspended = true;
    lowMemoryHandler(Critical::Yes);

    RefPtr callbackAggregator = CallbackAggregator::create([weakThis = WeakPtr { *this }, completionHandler = WTFMove(completionHandler)]() mutable {
        RELEASE_LOG(ProcessSuspension, "%p - NetworkProcess::prepareToSuspend() Process is ready to suspend", weakThis.get());
        completionHandler();
    });
    
    WebResourceLoadStatisticsStore::suspend([callbackAggregator] { });
    PCM::PersistentStore::prepareForProcessToSuspend([callbackAggregator] { });

    forEachNetworkSession([&] (auto& session) {
        platformFlushCookies(session.sessionID(), [callbackAggregator] { });
        session.storageManager().suspend([callbackAggregator] { });
    });

    for (auto& storageManager : m_closingStorageManagers)
        storageManager->suspend([callbackAggregator] { });
}

void NetworkProcess::applicationDidEnterBackground()
{
    m_downloadManager.applicationDidEnterBackground();
}

void NetworkProcess::applicationWillEnterForeground()
{
    m_downloadManager.applicationWillEnterForeground();
}

void NetworkProcess::processDidResume(bool forForegroundActivity)
{
    RELEASE_LOG(ProcessSuspension, "%p - NetworkProcess::processDidResume() forForegroundActivity=%d", this, forForegroundActivity);

    m_isSuspended = false;

    WebResourceLoadStatisticsStore::resume();
    PCM::PersistentStore::processDidResume();

    forEachNetworkSession([](auto& session) {
        session.storageManager().resume();
    });

    for (auto& storageManager : m_closingStorageManagers)
        storageManager->resume();
}

void NetworkProcess::prefetchDNS(const String& hostname)
{
    WebCore::prefetchDNS(hostname);
}

void NetworkProcess::registerURLSchemeAsSecure(const String& scheme) const
{
    LegacySchemeRegistry::registerURLSchemeAsSecure(scheme);
}

void NetworkProcess::registerURLSchemeAsBypassingContentSecurityPolicy(const String& scheme) const
{
    LegacySchemeRegistry::registerURLSchemeAsBypassingContentSecurityPolicy(scheme);
}

void NetworkProcess::registerURLSchemeAsLocal(const String& scheme) const
{
    LegacySchemeRegistry::registerURLSchemeAsLocal(scheme);
}

#if ENABLE(ALL_LEGACY_REGISTERED_SPECIAL_URL_SCHEMES)
void NetworkProcess::registerURLSchemeAsNoAccess(const String& scheme) const
{
    LegacySchemeRegistry::registerURLSchemeAsNoAccess(scheme);
}
#endif

void NetworkProcess::syncLocalStorage(CompletionHandler<void()>&& completionHandler)
{
    auto aggregator = CallbackAggregator::create(WTFMove(completionHandler));
    forEachNetworkSession([&](auto& session) {
        session.storageManager().syncLocalStorage([aggregator] { });
    });
}

void NetworkProcess::storeServiceWorkerRegistrations(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    CheckedPtr session = networkSession(sessionID);
    RefPtr server = session ? session->swServer() : nullptr;
    if (!server)
        return completionHandler();

    server->storeRegistrationsOnDisk(WTFMove(completionHandler));
}

void NetworkProcess::resetQuota(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        return session->storageManager().resetQuotaForTesting(WTFMove(completionHandler));

    completionHandler();
}

void NetworkProcess::setOriginQuotaRatioEnabledForTesting(PAL::SessionID sessionID, bool enabled, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        return session->storageManager().setOriginQuotaRatioEnabledForTesting(enabled, WTFMove(completionHandler));

    completionHandler();
}

void NetworkProcess::resetStoragePersistedState(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->storageManager().resetStoragePersistedState(WTFMove(completionHandler));
    else
        completionHandler();
}

void NetworkProcess::cloneSessionStorageForWebPage(PAL::SessionID sessionID, WebPageProxyIdentifier sourcePage, WebPageProxyIdentifier destinationPage)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->storageManager().cloneSessionStorageForWebPage(sourcePage, destinationPage);
}

void NetworkProcess::didIncreaseQuota(PAL::SessionID sessionID, ClientOrigin&& origin, QuotaIncreaseRequestIdentifier identifier, std::optional<uint64_t> newQuota)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->storageManager().didIncreaseQuota(WTFMove(origin), identifier, newQuota);
}

void NetworkProcess::renameOriginInWebsiteData(PAL::SessionID sessionID, SecurityOriginData&& oldOrigin, SecurityOriginData&& newOrigin, OptionSet<WebsiteDataType> dataTypes, CompletionHandler<void()>&& completionHandler)
{
    auto aggregator = CallbackAggregator::create(WTFMove(completionHandler));

    if (oldOrigin.isNull() || newOrigin.isNull())
        return;

    if (CheckedPtr session = networkSession(sessionID))
        session->storageManager().moveData(dataTypes, WTFMove(oldOrigin), WTFMove(newOrigin), [aggregator] { });
}

void NetworkProcess::websiteDataOriginDirectoryForTesting(PAL::SessionID sessionID, ClientOrigin&& origin, OptionSet<WebsiteDataType> dataType, CompletionHandler<void(const String&)>&& completionHandler)
{
    if (!dataType.hasExactlyOneBitSet()) {
        ASSERT_NOT_REACHED();
        return;
    }
    
    CheckedPtr session = networkSession(sessionID);
    if (!session)
        return completionHandler({ });

    session->storageManager().getOriginDirectory(WTFMove(origin), *dataType.toSingleValue(), WTFMove(completionHandler));
}

void NetworkProcess::processNotificationEvent(NotificationData&& data, NotificationEventType eventType, CompletionHandler<void(bool)>&& callback)
{
    CheckedPtr session = networkSession(data.sourceSession);
    if (!session) {
        callback(false);
        return;
    }

    session->ensureProtectedSWServer()->processNotificationEvent(WTFMove(data), eventType, WTFMove(callback));
}

void NetworkProcess::getAllBackgroundFetchIdentifiers(PAL::SessionID sessionID, CompletionHandler<void(Vector<String>&&)>&& callback)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        callback({ });
        return;
    }

    session->getAllBackgroundFetchIdentifiers(WTFMove(callback));
}

void NetworkProcess::getBackgroundFetchState(PAL::SessionID sessionID, const String& identifier, CompletionHandler<void(std::optional<BackgroundFetchState>&&)>&& callback)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        callback({ });
        return;
    }

    session->getBackgroundFetchState(identifier, WTFMove(callback));
}

void NetworkProcess::abortBackgroundFetch(PAL::SessionID sessionID, const String& identifier, CompletionHandler<void()>&& callback)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        callback();
        return;
    }

    session->abortBackgroundFetch(identifier, WTFMove(callback));
}

void NetworkProcess::pauseBackgroundFetch(PAL::SessionID sessionID, const String& identifier, CompletionHandler<void()>&& callback)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        callback();
        return;
    }

    session->pauseBackgroundFetch(identifier, WTFMove(callback));
}

void NetworkProcess::resumeBackgroundFetch(PAL::SessionID sessionID, const String& identifier, CompletionHandler<void()>&& callback)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        callback();
        return;
    }

    session->resumeBackgroundFetch(identifier, WTFMove(callback));
}

void NetworkProcess::clickBackgroundFetch(PAL::SessionID sessionID, const String& identifier, CompletionHandler<void()>&& callback)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        callback();
        return;
    }

    session->clickBackgroundFetch(identifier, WTFMove(callback));
}
#if ENABLE(WEB_PUSH_NOTIFICATIONS)

void NetworkProcess::getPendingPushMessage(PAL::SessionID sessionID, CompletionHandler<void(const std::optional<WebPushMessage>&)>&& callback)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        RELEASE_LOG(Push, "NetworkProcess getting pending push messages for session ID %" PRIu64, sessionID.toUInt64());
        session->notificationManager().getPendingPushMessage(WTFMove(callback));
        return;
    }

    RELEASE_LOG(Push, "NetworkProcess could not find session for ID %llu to get pending push messages", sessionID.toUInt64());
    callback(std::nullopt);
}

void NetworkProcess::getPendingPushMessages(PAL::SessionID sessionID, CompletionHandler<void(const Vector<WebPushMessage>&)>&& callback)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        LOG(Notifications, "NetworkProcess getting pending push messages for session ID %" PRIu64, sessionID.toUInt64());
        session->notificationManager().getPendingPushMessages(WTFMove(callback));
        return;
    }

    LOG(Notifications, "NetworkProcess could not find session for ID %llu to get pending push messages", sessionID.toUInt64());
    callback({ });
}

void NetworkProcess::processPushMessage(PAL::SessionID sessionID, WebPushMessage&& pushMessage, PushPermissionState permissionState, bool builtInNotficationsEnabled, CompletionHandler<void(bool, std::optional<WebCore::NotificationPayload>&&)>&& callback)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        RELEASE_LOG(Push, "Networking process handling a push message from UI process in session %llu", sessionID.toUInt64());
        auto origin = SecurityOriginData::fromURL(pushMessage.registrationURL);

        if (permissionState == PushPermissionState::Prompt) {
            RELEASE_LOG(Push, "Push message from %" SENSITIVE_LOG_STRING " won't be processed since permission is in the prompt state; removing push subscription", origin.toString().utf8().data());
            session->notificationManager().removePushSubscriptionsForOrigin(SecurityOriginData { origin }, [callback = WTFMove(callback)](auto&&) mutable {
                callback(false, std::nullopt);
            });
            return;
        }

        if (permissionState == PushPermissionState::Denied) {
            RELEASE_LOG(Push, "Push message from %" SENSITIVE_LOG_STRING " won't be processed since permission is in the denied state", origin.toString().utf8().data());
            // FIXME: move topic to ignore list in webpushd if permission is denied.
            callback(false, std::nullopt);
            return;
        }

        ASSERT(permissionState == PushPermissionState::Granted);
        auto scope = pushMessage.registrationURL.string();
        bool isDeclarative = !!pushMessage.notificationPayload;
        session->ensureProtectedSWServer()->processPushMessage(WTFMove(pushMessage.pushData), WTFMove(pushMessage.notificationPayload), WTFMove(pushMessage.registrationURL), [this, protectedThis = Ref { *this }, sessionID, origin = WTFMove(origin), scope = WTFMove(scope), callback = WTFMove(callback), isDeclarative, builtInNotficationsEnabled](bool result, std::optional<WebCore::NotificationPayload>&& resultPayload) mutable {
            // When using built-in notifications, we expect clients to use getPendingPushMessage, which automatically tracks silent push counts within webpushd.
            if (!builtInNotficationsEnabled &&!isDeclarative && !result) {
                if (CheckedPtr session = networkSession(sessionID)) {
                    session->notificationManager().incrementSilentPushCount(WTFMove(origin), [scope = WTFMove(scope), callback = WTFMove(callback), result](unsigned newSilentPushCount) mutable {
                        RELEASE_LOG_ERROR(Push, "Push message for scope %" SENSITIVE_LOG_STRING " not handled properly; new silent push count: %u", scope.utf8().data(), newSilentPushCount);
                        callback(result, std::nullopt);
                    });
                    return;
                }
            }

            callback(result, WTFMove(resultPayload));
        });
    } else {
        RELEASE_LOG_ERROR(Push, "Networking process asked to handle a push message from UI process in session %llu, but that session doesn't exist", sessionID.toUInt64());
        callback(false, WTFMove(pushMessage.notificationPayload));
    }
}

#else

void NetworkProcess::getPendingPushMessage(PAL::SessionID, CompletionHandler<void(const std::optional<WebPushMessage>&)>&& callback)
{
    callback({ });
}

void NetworkProcess::getPendingPushMessages(PAL::SessionID, CompletionHandler<void(const Vector<WebPushMessage>&)>&& callback)
{
    callback({ });
}

void NetworkProcess::processPushMessage(PAL::SessionID, WebPushMessage&&, PushPermissionState, bool, CompletionHandler<void(bool, std::optional<WebCore::NotificationPayload>&&)>&& callback)
{
    callback(false, std::nullopt);
}

#endif // ENABLE(WEB_PUSH_NOTIFICATIONS)

void NetworkProcess::setPushAndNotificationsEnabledForOrigin(PAL::SessionID sessionID, const SecurityOriginData& origin, bool enabled, CompletionHandler<void()>&& callback)
{
#if ENABLE(WEB_PUSH_NOTIFICATIONS)
    if (CheckedPtr session = networkSession(sessionID)) {
        session->notificationManager().setPushAndNotificationsEnabledForOrigin(origin, enabled, WTFMove(callback));
        return;
    }
#endif
    callback();
}

void NetworkProcess::removePushSubscriptionsForOrigin(PAL::SessionID sessionID, const SecurityOriginData& origin, CompletionHandler<void(unsigned)>&& callback)
{
#if ENABLE(WEB_PUSH_NOTIFICATIONS)
    if (CheckedPtr session = networkSession(sessionID)) {
        session->notificationManager().removePushSubscriptionsForOrigin(SecurityOriginData { origin }, WTFMove(callback));
        return;
    }
#endif
    callback(0);
}

void NetworkProcess::hasPushSubscriptionForTesting(PAL::SessionID sessionID, URL&& scopeURL, CompletionHandler<void(bool)>&& callback)
{
#if ENABLE(WEB_PUSH_NOTIFICATIONS)
    if (CheckedPtr session = networkSession(sessionID)) {
        session->notificationManager().getPushSubscription(WTFMove(scopeURL), [callback = WTFMove(callback)](auto &&result) mutable {
            callback(result && result->has_value());
        });
        return;
    }
#endif

    callback(false);
}

void NetworkProcess::getAppBadgeForTesting(PAL::SessionID sessionID, CompletionHandler<void(std::optional<uint64_t>)>&& callback)
{
#if ENABLE(WEB_PUSH_NOTIFICATIONS)
    if (CheckedPtr session = networkSession(sessionID)) {
        session->notificationManager().getAppBadgeForTesting(WTFMove(callback));
        return;
    }
#endif

    callback(std::nullopt);
}

#if ENABLE(INSPECTOR_NETWORK_THROTTLING)

void NetworkProcess::setEmulatedConditions(PAL::SessionID sessionID, std::optional<int64_t>&& bytesPerSecondLimit)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setEmulatedConditions(WTFMove(bytesPerSecondLimit));
}

#endif // ENABLE(INSPECTOR_NETWORK_THROTTLING)

#if !PLATFORM(COCOA)
void NetworkProcess::initializeProcess(const AuxiliaryProcessInitializationParameters&)
{
}

void NetworkProcess::initializeProcessName(const AuxiliaryProcessInitializationParameters&)
{
}

void NetworkProcess::initializeSandbox(const AuxiliaryProcessInitializationParameters&, SandboxInitializationParameters&)
{
}

void NetworkProcess::flushCookies(PAL::SessionID, CompletionHandler<void()>&& completionHandler)
{
    completionHandler();
}

void NetworkProcess::platformFlushCookies(PAL::SessionID, CompletionHandler<void()>&& completionHandler)
{
    completionHandler();
}

#endif

void NetworkProcess::storePrivateClickMeasurement(PAL::SessionID sessionID, WebCore::PrivateClickMeasurement&& privateClickMeasurement)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->storePrivateClickMeasurement(WTFMove(privateClickMeasurement));
}

void NetworkProcess::dumpPrivateClickMeasurement(PAL::SessionID sessionID, CompletionHandler<void(String)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        return session->dumpPrivateClickMeasurement(WTFMove(completionHandler));

    completionHandler({ });
}

void NetworkProcess::clearPrivateClickMeasurement(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->clearPrivateClickMeasurement(WTFMove(completionHandler));
    else
        completionHandler();
}

bool NetworkProcess::allowsPrivateClickMeasurementTestFunctionality() const
{
#if !PLATFORM(COCOA) || !USE(APPLE_INTERNAL_SDK)
    return true;
#else
    auto auditToken = sourceApplicationAuditToken();
    if (!auditToken)
        return false;
    return WTF::hasEntitlement(*auditToken, "com.apple.private.webkit.adattributiond.testing"_s);
#endif
}

void NetworkProcess::setPrivateClickMeasurementOverrideTimerForTesting(PAL::SessionID sessionID, bool value, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID))
        session->setPrivateClickMeasurementOverrideTimerForTesting(value);
    
    completionHandler();
}

void NetworkProcess::closePCMDatabase(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        session->destroyPrivateClickMeasurementStore(WTFMove(completionHandler));
        return;
    }

    completionHandler();
}

void NetworkProcess::simulatePrivateClickMeasurementSessionRestart(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID)) {
        session->destroyPrivateClickMeasurementStore([session = WeakPtr { *session }, completionHandler = WTFMove(completionHandler)] () mutable {
            if (session)
                session->firePrivateClickMeasurementTimerImmediatelyForTesting();
            completionHandler();
        });
        return;
    }
    completionHandler();
}

void NetworkProcess::markAttributedPrivateClickMeasurementsAsExpiredForTesting(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID)) {
        session->markAttributedPrivateClickMeasurementsAsExpiredForTesting(WTFMove(completionHandler));
        return;
    }
    completionHandler();
}

void NetworkProcess::setPrivateClickMeasurementEphemeralMeasurementForTesting(PAL::SessionID sessionID, bool value, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID))
        session->setPrivateClickMeasurementEphemeralMeasurementForTesting(value);
    
    completionHandler();
}


void NetworkProcess::setPrivateClickMeasurementTokenPublicKeyURLForTesting(PAL::SessionID sessionID, URL&& url, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID))
        session->setPrivateClickMeasurementTokenPublicKeyURLForTesting(WTFMove(url));

    completionHandler();
}

void NetworkProcess::setPrivateClickMeasurementTokenSignatureURLForTesting(PAL::SessionID sessionID, URL&& url, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID))
        session->setPrivateClickMeasurementTokenSignatureURLForTesting(WTFMove(url));
    
    completionHandler();
}

void NetworkProcess::setPrivateClickMeasurementAttributionReportURLsForTesting(PAL::SessionID sessionID, URL&& sourceURL, URL&& destinationURL, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID))
        session->setPrivateClickMeasurementAttributionReportURLsForTesting(WTFMove(sourceURL), WTFMove(destinationURL));

    completionHandler();
}

void NetworkProcess::markPrivateClickMeasurementsAsExpiredForTesting(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID))
        session->markPrivateClickMeasurementsAsExpiredForTesting();

    completionHandler();
}

void NetworkProcess::setPCMFraudPreventionValuesForTesting(PAL::SessionID sessionID, String&& unlinkableToken, String&& secretToken, String&& signature, String&& keyID, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID))
        session->setPCMFraudPreventionValuesForTesting(WTFMove(unlinkableToken), WTFMove(secretToken), WTFMove(signature), WTFMove(keyID));

    completionHandler();
}

void NetworkProcess::setPrivateClickMeasurementAppBundleIDForTesting(PAL::SessionID sessionID, String&& appBundleIDForTesting, CompletionHandler<void()>&& completionHandler)
{
    if (!allowsPrivateClickMeasurementTestFunctionality())
        return completionHandler();

    if (CheckedPtr session = networkSession(sessionID))
        session->setPrivateClickMeasurementAppBundleIDForTesting(WTFMove(appBundleIDForTesting));

    completionHandler();
}

void NetworkProcess::addKeptAliveLoad(Ref<NetworkResourceLoader>&& loader)
{
    if (CheckedPtr session = networkSession(loader->sessionID()))
        session->addKeptAliveLoad(WTFMove(loader));
}

void NetworkProcess::removeKeptAliveLoad(NetworkResourceLoader& loader)
{
    if (CheckedPtr session = networkSession(loader.sessionID()))
        session->removeKeptAliveLoad(loader);
}

void NetworkProcess::connectionToWebProcessClosed(IPC::Connection& connection, PAL::SessionID sessionID)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->storageManager().stopReceivingMessageFromConnection(connection);
}

NetworkConnectionToWebProcess* NetworkProcess::webProcessConnection(ProcessIdentifier identifier) const
{
    return m_webProcessConnections.get(identifier);
}

RefPtr<NetworkConnectionToWebProcess> NetworkProcess::protectedWebProcessConnection(WebCore::ProcessIdentifier identifier) const
{
    return webProcessConnection(identifier);
}

NetworkConnectionToWebProcess* NetworkProcess::webProcessConnection(const IPC::Connection& connection) const
{
    for (Ref webProcessConnection : m_webProcessConnections.values()) {
        if (webProcessConnection->connection().uniqueID() == connection.uniqueID())
            return webProcessConnection.ptr();
    }
    return nullptr;
}

const Seconds NetworkProcess::defaultServiceWorkerFetchTimeout = 70_s;
void NetworkProcess::setServiceWorkerFetchTimeoutForTesting(Seconds timeout, CompletionHandler<void()>&& completionHandler)
{
    m_serviceWorkerFetchTimeout = timeout;
    completionHandler();
}

void NetworkProcess::resetServiceWorkerFetchTimeoutForTesting(CompletionHandler<void()>&& completionHandler)
{
    m_serviceWorkerFetchTimeout = defaultServiceWorkerFetchTimeout;
    completionHandler();
}

void NetworkProcess::terminateIdleServiceWorkers(WebCore::ProcessIdentifier processIdentifier, CompletionHandler<void()>&& callback)
{
    if (RefPtr connection = webProcessConnection(processIdentifier))
        connection->terminateIdleServiceWorkers();
    callback();
}

Seconds NetworkProcess::randomClosedPortDelay()
{
    // Random delay in the range [10ms, 110ms).
    return 10_ms + Seconds { cryptographicallyRandomUnitInterval() * (100_ms).value() };
}

#if ENABLE(APP_BOUND_DOMAINS)
void NetworkProcess::hasAppBoundSession(PAL::SessionID sessionID, CompletionHandler<void(bool)>&& completionHandler) const
{
    bool result = false;
    if (CheckedPtr session = networkSession(sessionID))
        result = session->hasAppBoundSession();
    completionHandler(result);
}

void NetworkProcess::clearAppBoundSession(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        session->clearAppBoundSession();
        completionHandler();
    } else {
        ASSERT_NOT_REACHED();
        completionHandler();
    }
}
#endif

void NetworkProcess::broadcastConsoleMessage(PAL::SessionID sessionID, JSC::MessageSource source, JSC::MessageLevel level, const String& message)
{
    for (auto& networkConnectionToWebProcess : m_webProcessConnections.values()) {
        if (networkConnectionToWebProcess->sessionID() == sessionID)
            networkConnectionToWebProcess->broadcastConsoleMessage(source, level, message);
    }
}

void NetworkProcess::updateBundleIdentifier(String&& bundleIdentifier, CompletionHandler<void()>&& completionHandler)
{
#if PLATFORM(COCOA)
    clearApplicationBundleIdentifierTestingOverride();
    setApplicationBundleIdentifierOverride(bundleIdentifier);
#endif
    completionHandler();
}

void NetworkProcess::clearBundleIdentifier(CompletionHandler<void()>&& completionHandler)
{
#if PLATFORM(COCOA)
    clearApplicationBundleIdentifierTestingOverride();
#endif
    completionHandler();
}

bool NetworkProcess::shouldDisableCORSForRequestTo(PageIdentifier pageIdentifier, const URL& url) const
{
    return std::ranges::any_of(m_extensionCORSDisablingPatterns.get(pageIdentifier), [&](auto& pattern) {
        return pattern.matches(url);
    });
}

void NetworkProcess::setCORSDisablingPatterns(NetworkConnectionToWebProcess& connection, PageIdentifier pageIdentifier, Vector<String>&& patterns)
{
    auto parsedPatterns = WTF::compactMap(WTFMove(patterns), [&](auto&& pattern) -> std::optional<UserContentURLPattern> {
        UserContentURLPattern parsedPattern(WTFMove(pattern));
        if (parsedPattern.isValid()) {
            connection.originAccessPatterns().allowAccessTo(parsedPattern);
            return parsedPattern;
        }
        return std::nullopt;
    });

    parsedPatterns.shrinkToFit();

    if (parsedPatterns.isEmpty()) {
        m_extensionCORSDisablingPatterns.remove(pageIdentifier);
        return;
    }

    m_extensionCORSDisablingPatterns.set(pageIdentifier, WTFMove(parsedPatterns));
}

#if PLATFORM(COCOA)
void NetworkProcess::appPrivacyReportTestingData(PAL::SessionID sessionID, CompletionHandler<void(const AppPrivacyReportTestingData&)>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID)) {
        completionHandler(session->appPrivacyReportTestingData());
        return;
    }
    completionHandler({ });
}

void NetworkProcess::clearAppPrivacyReportTestingData(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->appPrivacyReportTestingData().clearAppPrivacyReportTestingData();

    completionHandler();
}
#endif

#if ENABLE(WEB_RTC)
RTCDataChannelRemoteManagerProxy& NetworkProcess::rtcDataChannelProxy()
{
    ASSERT(isMainRunLoop());
    if (!m_rtcDataChannelProxy)
        m_rtcDataChannelProxy = RTCDataChannelRemoteManagerProxy::create(*this);
    return *m_rtcDataChannelProxy;
}

Ref<RTCDataChannelRemoteManagerProxy> NetworkProcess::protectedRTCDataChannelProxy()
{
    return rtcDataChannelProxy();
}
#endif

void NetworkProcess::addWebPageNetworkParameters(PAL::SessionID sessionID, WebPageProxyIdentifier pageID, WebPageNetworkParameters&& parameters)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->addWebPageNetworkParameters(pageID, WTFMove(parameters));
}

void NetworkProcess::removeWebPageNetworkParameters(PAL::SessionID sessionID, WebPageProxyIdentifier pageID)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session)
        return;

    session->removeWebPageNetworkParameters(pageID);
    session->storageManager().clearStorageForWebPage(pageID);

    if (RefPtr resourceLoadStatistics = session->resourceLoadStatistics())
        resourceLoadStatistics->clearFrameLoadRecordsForStorageAccess(pageID);

    m_pagesWithRelaxedThirdPartyCookieBlocking.remove(pageID);
}

void NetworkProcess::countNonDefaultSessionSets(PAL::SessionID sessionID, CompletionHandler<void(uint64_t)>&& completionHandler)
{
    CheckedPtr session = networkSession(sessionID);
    completionHandler(session ? session->countNonDefaultSessionSets() : 0);
}

void NetworkProcess::allowFilesAccessFromWebProcess(WebCore::ProcessIdentifier processID, const Vector<String>& paths, CompletionHandler<void()>&& completionHandler)
{
    if (RefPtr connection = webProcessConnection(processID)) {
        for (auto& path : paths)
            connection->allowAccessToFile(path);
    }
    completionHandler();
}

void NetworkProcess::allowFileAccessFromWebProcess(WebCore::ProcessIdentifier processID, const String& path, CompletionHandler<void()>&& completionHandler)
{
    if (RefPtr connection = webProcessConnection(processID))
        connection->allowAccessToFile(path);
    completionHandler();
}

void NetworkProcess::requestBackgroundFetchPermission(PAL::SessionID sessionID, const ClientOrigin& origin, CompletionHandler<void(bool)>&& callback)
{
    protectedParentProcessConnection()->sendWithAsyncReply(Messages::NetworkProcessProxy::RequestBackgroundFetchPermission(sessionID, origin), WTFMove(callback));
}

#if USE(RUNNINGBOARD)
void NetworkProcess::setIsHoldingLockedFiles(bool isHoldingLockedFiles)
{
#if PLATFORM(MAC)
    // The sandbox doesn't allow the network process to talk to runningboardd on macOS.
    UNUSED_PARAM(isHoldingLockedFiles);
#else
    if (!isHoldingLockedFiles) {
        m_holdingLockedFileAssertion = nullptr;
        return;
    }

    if (m_holdingLockedFileAssertion && m_holdingLockedFileAssertion->isValid())
        return;

    // We synchronously take a process assertion when beginning a SQLite transaction so that we don't get suspended
    // while holding a locked file. We would get killed if suspended while holding locked files.
    m_holdingLockedFileAssertion = ProcessAssertion::create(getCurrentProcessID(), "Network Process is holding locked files"_s, ProcessAssertionType::FinishTaskCanSleep, ProcessAssertion::Mode::Sync);
#endif
}
#endif

void NetworkProcess::setInspectionForServiceWorkersAllowed(PAL::SessionID sessionID, bool inspectable)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setInspectionForServiceWorkersAllowed(inspectable);
}

void NetworkProcess::setStorageSiteValidationEnabled(PAL::SessionID sessionID, bool enabled)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->storageManager().setStorageSiteValidationEnabled(enabled);
}

void NetworkProcess::setPersistedDomains(PAL::SessionID sessionID, HashSet<RegistrableDomain>&& domains)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->setPersistedDomains(WTFMove(domains));
}

void NetworkProcess::fetchLocalStorage(PAL::SessionID sessionID, CompletionHandler<void(std::optional<HashMap<WebCore::ClientOrigin, HashMap<String, String>>>&&)>&& completionHandler)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        completionHandler(std::nullopt);
        return;
    }

    session->storageManager().fetchLocalStorage(WTFMove(completionHandler));
}

void NetworkProcess::restoreLocalStorage(PAL::SessionID sessionID, HashMap<WebCore::ClientOrigin, HashMap<String, String>>&& localStorageMap, CompletionHandler<void(bool)>&& completionHandler)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        completionHandler(false);
        return;
    }

    session->storageManager().restoreLocalStorage(WTFMove(localStorageMap), WTFMove(completionHandler));
}

void NetworkProcess::fetchSessionStorage(PAL::SessionID sessionID, WebPageProxyIdentifier pageID, CompletionHandler<void(std::optional<HashMap<WebCore::ClientOrigin, HashMap<String, String>>>&&)>&& completionHandler)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        completionHandler(std::nullopt);
        return;
    }

    session->storageManager().fetchSessionStorageForWebPage(pageID, WTFMove(completionHandler));
}

void NetworkProcess::restoreSessionStorage(PAL::SessionID sessionID, WebPageProxyIdentifier pageID, HashMap<WebCore::ClientOrigin, HashMap<String, String>>&& sessionStorageMap, CompletionHandler<void(bool)>&& completionHandler)
{
    CheckedPtr session = networkSession(sessionID);
    if (!session) {
        completionHandler(false);
        return;
    }

    session->storageManager().restoreSessionStorageForWebPage(pageID, WTFMove(sessionStorageMap), WTFMove(completionHandler));
}

void NetworkProcess::setShouldRelaxThirdPartyCookieBlockingForPage(WebPageProxyIdentifier pageID)
{
    m_pagesWithRelaxedThirdPartyCookieBlocking.add(pageID);
}

ShouldRelaxThirdPartyCookieBlocking NetworkProcess::shouldRelaxThirdPartyCookieBlockingForPage(std::optional<WebPageProxyIdentifier> pageID) const
{
    return pageID && m_pagesWithRelaxedThirdPartyCookieBlocking.contains(*pageID) ? ShouldRelaxThirdPartyCookieBlocking::Yes : ShouldRelaxThirdPartyCookieBlocking::No;
}

#if ENABLE(CONTENT_EXTENSIONS)
void NetworkProcess::resetResourceMonitorThrottlerForTesting(PAL::SessionID sessionID, CompletionHandler<void()>&& completionHandler)
{
    if (CheckedPtr session = networkSession(sessionID))
        session->clearResourceMonitorThrottlerData(WTFMove(completionHandler));
    else
        completionHandler();
}
#endif

void NetworkProcess::setDefaultRequestTimeoutInterval(double timeoutInterval)
{
    ResourceRequestBase::setDefaultTimeoutInterval(timeoutInterval);
}

} // namespace WebKit
