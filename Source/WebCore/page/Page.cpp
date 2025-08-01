/*
 * Copyright (C) 2006-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "Page.h"

#include "AXLogger.h"
#include "ActivityStateChangeObserver.h"
#include "AdvancedPrivacyProtections.h"
#include "AlternativeTextClient.h"
#include "AnchorPositionEvaluator.h"
#include "AnimationFrameRate.h"
#include "AnimationTimelinesController.h"
#include "AppHighlightStorage.h"
#include "ApplicationCacheStorage.h"
#include "ArchiveResource.h"
#include "AsyncNodeDeletionQueueInlines.h"
#include "AttachmentElementClient.h"
#include "AuthenticatorCoordinator.h"
#include "AuthenticatorCoordinatorClient.h"
#include "BackForwardCache.h"
#include "BackForwardClient.h"
#include "BackForwardController.h"
#include "BadgeClient.h"
#include "BoundaryPointInlines.h"
#include "BroadcastChannelRegistry.h"
#include "CacheStorageProvider.h"
#include "CachedImage.h"
#include "CachedResourceLoader.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "CommonAtomStrings.h"
#include "CommonVM.h"
#include "ConstantPropertyMap.h"
#include "ContainerNodeInlines.h"
#include "ContextMenuClient.h"
#include "ContextMenuController.h"
#include "CookieJar.h"
#include "CredentialRequestCoordinator.h"
#include "CryptoClient.h"
#include "DOMRect.h"
#include "DOMRectList.h"
#include "DatabaseProvider.h"
#include "DebugOverlayRegions.h"
#include "DebugPageOverlays.h"
#include "DiagnosticLoggingClient.h"
#include "DiagnosticLoggingKeys.h"
#include "DisplayRefreshMonitorManager.h"
#include "DocumentFullscreen.h"
#include "DocumentInlines.h"
#include "DocumentLoader.h"
#include "DocumentMarkerController.h"
#include "DocumentSyncData.h"
#include "DragController.h"
#include "Editing.h"
#include "Editor.h"
#include "EditorClient.h"
#include "ElementTargetingController.h"
#include "EmptyClients.h"
#include "Event.h"
#include "EventHandler.h"
#include "EventLoop.h"
#include "EventNames.h"
#include "ExtensionStyleSheets.h"
#include "FilterRenderingMode.h"
#include "FixedContainerEdges.h"
#include "FocusController.h"
#include "FontCache.h"
#include "FragmentDirectiveGenerator.h"
#include "FrameLoader.h"
#include "FrameSelection.h"
#include "FrameTree.h"
#include "GeolocationController.h"
#include "HTMLElement.h"
#include "HTMLImageElement.h"
#include "HTMLMediaElement.h"
#include "HTMLTextAreaElement.h"
#include "HTMLTextFormControlElement.h"
#include "HistoryController.h"
#include "HistoryItem.h"
#include "IDBConnectionToServer.h"
#include "ImageAnalysisQueue.h"
#include "ImageOverlay.h"
#include "ImageOverlayController.h"
#include "InspectorBackendClient.h"
#include "InspectorController.h"
#include "InspectorInstrumentation.h"
#include "IntelligenceTextEffectsSupport.h"
#include "LayoutDisallowedScope.h"
#include "LegacySchemeRegistry.h"
#include "LoaderStrategy.h"
#include "LocalFrameLoaderClient.h"
#include "LocalFrameViewInlines.h"
#include "LogInitialization.h"
#include "Logging.h"
#include "LoginStatus.h"
#include "LowPowerModeNotifier.h"
#include "MediaCanStartListener.h"
#include "MediaEngineConfigurationFactory.h"
#include "MemoryCache.h"
#include "ModelPlayerProvider.h"
#include "NavigationScheduler.h"
#include "Navigator.h"
#include "NavigatorGamepad.h"
#include "OpportunisticTaskScheduler.h"
#include "PageColorSampler.h"
#include "PageConfiguration.h"
#include "PageConsoleClient.h"
#include "PageDebuggable.h"
#include "PageGroup.h"
#include "PageOverlayController.h"
#include "PaymentCoordinator.h"
#include "PerformanceLogging.h"
#include "PerformanceLoggingClient.h"
#include "PerformanceMonitor.h"
#include "PlatformMediaSessionManager.h"
#include "PlatformScreen.h"
#include "PlatformStrategies.h"
#include "PluginData.h"
#include "PluginInfoProvider.h"
#include "PluginViewBase.h"
#include "PointerCaptureController.h"
#include "PointerLockController.h"
#include "ProcessSyncClient.h"
#include "ProcessSyncData.h"
#include "ProgressTracker.h"
#include "RTCController.h"
#include "Range.h"
#include "RemoteFrame.h"
#include "RenderDescendantIterator.h"
#include "RenderElementInlines.h"
#include "RenderImage.h"
#include "RenderLayerCompositor.h"
#include "RenderObjectInlines.h"
#include "RenderTheme.h"
#include "RenderView.h"
#include "RenderWidget.h"
#include "RenderingUpdateScheduler.h"
#include "ResizeObserver.h"
#include "ResourceLoadObserver.h"
#include "ResourceUsageOverlay.h"
#include "SVGDocumentExtensions.h"
#include "SVGImage.h"
#include "ScreenOrientationManager.h"
#include "ScriptController.h"
#include "ScriptDisallowedScope.h"
#include "ScriptRunner.h"
#include "ScriptedAnimationController.h"
#include "ScrollLatchingController.h"
#include "ScrollingCoordinator.h"
#include "ServiceWorkerGlobalScope.h"
#include "Settings.h"
#include "SharedBuffer.h"
#include "SocketProvider.h"
#include "SpeechRecognitionProvider.h"
#include "SpeechSynthesisClient.h"
#include "StorageArea.h"
#include "StorageNamespace.h"
#include "StorageNamespaceProvider.h"
#include "StorageProvider.h"
#include "StyleAdjuster.h"
#include "StyleResolver.h"
#include "StyleScope.h"
#include "SubframeLoader.h"
#include "SubresourceLoader.h"
#include "TextExtraction.h"
#include "TextIterator.h"
#include "TextRecognitionResult.h"
#include "TextResourceDecoder.h"
#include "ThermalMitigationNotifier.h"
#include "UserContentProvider.h"
#include "UserContentURLPattern.h"
#include "UserMediaController.h"
#include "UserScript.h"
#include "UserStyleSheet.h"
#include "ValidationMessageClient.h"
#include "VisibilityState.h"
#include "VisitedLinkState.h"
#include "VisitedLinkStore.h"
#include "VoidCallback.h"
#include "WebCoreJSClientData.h"
#include "WebRTCProvider.h"
#include "WheelEventDeltaFilter.h"
#include "WheelEventTestMonitor.h"
#include "Widget.h"
#include "WindowEventLoop.h"
#include "WindowFeatures.h"
#include "WorkerOrWorkletScriptController.h"
#include <JavaScriptCore/VM.h>
#include <ranges>
#include <wtf/FileSystem.h>
#include <wtf/RefCountedLeakCounter.h>
#include <wtf/StdLibExtras.h>
#include <wtf/SystemTracing.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/Base64.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/TextStream.h>

#if ENABLE(APPLE_PAY_AMS_UI)
#include "ApplePayAMSUIPaymentHandler.h"
#endif

#if ENABLE(WIRELESS_PLAYBACK_TARGET)
#include "HTMLVideoElement.h"
#include "MediaPlaybackTarget.h"
#endif

#if PLATFORM(MAC)
#include "ServicesOverlayController.h"
#endif

#if ENABLE(MEDIA_SESSION_COORDINATOR)
#include "MediaSessionCoordinator.h"
#include "NavigatorMediaSession.h"
#endif

#if USE(ATSPI)
#include "AccessibilityRootAtspi.h"
#endif

#if ENABLE(WRITING_TOOLS)
#include "WritingToolsController.h"
#endif

#if ENABLE(WEBXR)
#include "NavigatorWebXR.h"
#include "WebXRSession.h"
#include "WebXRSystem.h"
#endif

#if PLATFORM(VISION) && ENABLE(GAMEPAD)
#include "GamepadManager.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(Page);

static HashSet<WeakRef<Page>>& allPages()
{
    static NeverDestroyed<HashSet<WeakRef<Page>>> set;
    return set;
}

static unsigned gNonUtilityPageCount { 0 };

static inline bool isUtilityPageChromeClient(ChromeClient& chromeClient)
{
    return chromeClient.isEmptyChromeClient() || chromeClient.isSVGImageChromeClient();
}

unsigned Page::nonUtilityPageCount()
{
    return gNonUtilityPageCount;
}

DEFINE_DEBUG_ONLY_GLOBAL(WTF::RefCountedLeakCounter, pageCounter, ("Page"));

void Page::forEachPage(NOESCAPE const Function<void(Page&)>& function)
{
    for (auto& page : allPages())
        function(Ref { page.get() });
}

void Page::updateValidationBubbleStateIfNeeded()
{
    if (auto* client = validationMessageClient())
        client->updateValidationBubbleStateIfNeeded();
}

void Page::scheduleValidationMessageUpdate(ValidatedFormListedElement& element, HTMLElement& anchor)
{
    m_validationMessageUpdates.append({ element, anchor });
}

void Page::updateValidationMessages()
{
    for (auto& item : std::exchange(m_validationMessageUpdates, { })) {
        if (RefPtr anchor = item.second.get())
            Ref { item.first }->updateVisibleValidationMessage(*anchor);
    }
}

static void networkStateChanged(bool isOnLine)
{
    Vector<Ref<LocalFrame>> frames;

    // Get all the frames of all the pages in all the page groups
    for (auto& page : allPages()) {
        for (RefPtr frame = page->mainFrame(); frame; frame = frame->tree().traverseNext()) {
            if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame))
                frames.append(*localFrame);
        }
    }

    auto& eventName = isOnLine ? eventNames().onlineEvent : eventNames().offlineEvent;
    for (auto& frame : frames) {
        if (RefPtr document = frame->document())
            document->dispatchWindowEvent(Event::create(eventName, Event::CanBubble::No, Event::IsCancelable::No));
    }
}

static constexpr OptionSet<ActivityState> pageInitialActivityState()
{
    return { ActivityState::IsVisible, ActivityState::IsInWindow };
}

// FIXME: workaround for GCC bug https://gcc.gnu.org/bugzilla/show_bug.cgi?id=115135
#if COMPILER(GCC) && CPU(ARM64)
#define GCC_MAYBE_NO_INLINE NEVER_INLINE
#else
#define GCC_MAYBE_NO_INLINE
#endif

GCC_MAYBE_NO_INLINE static Ref<Frame> createMainFrame(Page& page, PageConfiguration::MainFrameCreationParameters&& clientCreator, RefPtr<Frame> mainFrameOpener, FrameIdentifier identifier, Ref<FrameTreeSyncData>&& frameTreeSyncData)
{
    page.relaxAdoptionRequirement();
    return switchOn(WTFMove(clientCreator), [&] (PageConfiguration::LocalMainFrameCreationParameters&& creationParameters) -> Ref<Frame> {
        return LocalFrame::createMainFrame(page, WTFMove(creationParameters.clientCreator), identifier, creationParameters.effectiveSandboxFlags, mainFrameOpener.get(), WTFMove(frameTreeSyncData));
    }, [&] (CompletionHandler<UniqueRef<RemoteFrameClient>(RemoteFrame&)>&& remoteFrameClientCreator) -> Ref<Frame> {
        return RemoteFrame::createMainFrame(page, WTFMove(remoteFrameClientCreator), identifier, mainFrameOpener.get(), WTFMove(frameTreeSyncData));
    });
}

Ref<Page> Page::create(PageConfiguration&& pageConfiguration)
{
    return adoptRef(*new Page(WTFMove(pageConfiguration)));
}

struct Page::Internals {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(Page);

    Region topRelevantPaintedRegion;
    Region bottomRelevantPaintedRegion;
    Region relevantUnpaintedRegion;
};

Page::Page(PageConfiguration&& pageConfiguration)
    : m_internals(makeUniqueRef<Internals>())
    , m_identifier(pageConfiguration.identifier)
    , m_chrome(makeUniqueRef<Chrome>(*this, WTFMove(pageConfiguration.chromeClient)))
    , m_dragCaretController(makeUniqueRef<DragCaretController>())
#if ENABLE(DRAG_SUPPORT)
    , m_dragController(makeUniqueRef<DragController>(*this, WTFMove(pageConfiguration.dragClient)))
#endif
    , m_focusController(makeUniqueRef<FocusController>(*this, pageInitialActivityState()))
#if ENABLE(CONTEXT_MENUS)
    , m_contextMenuController(makeUniqueRef<ContextMenuController>(*this, WTFMove(pageConfiguration.contextMenuClient)))
#endif
    , m_inspectorController(makeUniqueRefWithoutRefCountedCheck<InspectorController>(*this, WTFMove(pageConfiguration.inspectorBackendClient)))
    , m_pointerCaptureController(makeUniqueRef<PointerCaptureController>(*this))
#if ENABLE(POINTER_LOCK)
    , m_pointerLockController(makeUniqueRefWithoutRefCountedCheck<PointerLockController>(*this))
#endif
    , m_elementTargetingController(makeUniqueRef<ElementTargetingController>(*this))
    , m_settings(Settings::create(this))
    , m_cryptoClient(WTFMove(pageConfiguration.cryptoClient))
    , m_progress(makeUniqueRef<ProgressTracker>(*this, WTFMove(pageConfiguration.progressTrackerClient)))
    , m_processSyncClient(WTFMove(pageConfiguration.processSyncClient))
    , m_backForwardController(makeUniqueRef<BackForwardController>(*this, WTFMove(pageConfiguration.backForwardClient)))
    , m_editorClient(WTFMove(pageConfiguration.editorClient))
    , m_mainFrame(createMainFrame(*this, WTFMove(pageConfiguration.mainFrameCreationParameters), WTFMove(pageConfiguration.mainFrameOpener), pageConfiguration.mainFrameIdentifier, FrameTreeSyncData::create()))
    , m_validationMessageClient(WTFMove(pageConfiguration.validationMessageClient))
    , m_diagnosticLoggingClient(WTFMove(pageConfiguration.diagnosticLoggingClient))
    , m_performanceLoggingClient(WTFMove(pageConfiguration.performanceLoggingClient))
#if ENABLE(SPEECH_SYNTHESIS)
    , m_speechSynthesisClient(WTFMove(pageConfiguration.speechSynthesisClient))
#endif
    , m_speechRecognitionProvider((WTFMove(pageConfiguration.speechRecognitionProvider)))
    , m_webRTCProvider(WTFMove(pageConfiguration.webRTCProvider))
    , m_rtcController(RTCController::create())
#if PLATFORM(IOS_FAMILY)
    , m_canShowWhileLocked(pageConfiguration.canShowWhileLocked)
#endif
    , m_domTimerAlignmentInterval(DOMTimer::defaultAlignmentInterval())
    , m_domTimerAlignmentIntervalIncreaseTimer(*this, &Page::domTimerAlignmentIntervalIncreaseTimerFired)
    , m_activityState(pageInitialActivityState())
    , m_alternativeTextClient(WTFMove(pageConfiguration.alternativeTextClient))
    , m_consoleClient(makeUniqueRef<PageConsoleClient>(*this))
#if ENABLE(REMOTE_INSPECTOR)
    , m_inspectorDebuggable(PageDebuggable::create(*this))
#endif
    , m_socketProvider(WTFMove(pageConfiguration.socketProvider))
    , m_cookieJar(WTFMove(pageConfiguration.cookieJar))
    , m_applicationCacheStorage(WTFMove(pageConfiguration.applicationCacheStorage))
    , m_cacheStorageProvider(WTFMove(pageConfiguration.cacheStorageProvider))
    , m_databaseProvider(*WTFMove(pageConfiguration.databaseProvider))
    , m_pluginInfoProvider(*WTFMove(pageConfiguration.pluginInfoProvider))
    , m_storageNamespaceProvider(*WTFMove(pageConfiguration.storageNamespaceProvider))
    , m_userContentProvider(WTFMove(pageConfiguration.userContentProvider))
    , m_screenOrientationManager(WTFMove(pageConfiguration.screenOrientationManager))
    , m_visitedLinkStore(*WTFMove(pageConfiguration.visitedLinkStore))
    , m_broadcastChannelRegistry(WTFMove(pageConfiguration.broadcastChannelRegistry))
    , m_sessionID(pageConfiguration.sessionID)
#if ENABLE(VIDEO)
    , m_playbackControlsManagerUpdateTimer(*this, &Page::playbackControlsManagerUpdateTimerFired)
#endif
    , m_isUtilityPage(isUtilityPageChromeClient(chrome().client()))
    , m_performanceMonitor(isUtilityPage() ? nullptr : makeUniqueWithoutRefCountedCheck<PerformanceMonitor>(*this))
    , m_lowPowerModeNotifier(makeUniqueRef<LowPowerModeNotifier>([this](bool isLowPowerModeEnabled) { handleLowPowerModeChange(isLowPowerModeEnabled); }))
    , m_thermalMitigationNotifier(makeUniqueRef<ThermalMitigationNotifier>([this](bool thermalMitigationEnabled) { handleThermalMitigationChange(thermalMitigationEnabled); }))
    , m_performanceLogging(makeUniqueRef<PerformanceLogging>(*this))
#if PLATFORM(MAC) && (ENABLE(SERVICE_CONTROLS) || ENABLE(TELEPHONE_NUMBER_DETECTION))
    , m_servicesOverlayController(makeUniqueRefWithoutRefCountedCheck<ServicesOverlayController>(*this))
#endif
    , m_recentWheelEventDeltaFilter(WheelEventDeltaFilter::create())
    , m_pageOverlayController(makeUniqueRef<PageOverlayController>(*this))
#if ENABLE(APPLE_PAY)
    , m_paymentCoordinator(PaymentCoordinator::create(WTFMove(pageConfiguration.paymentCoordinatorClient)))
#endif
#if ENABLE(WEB_AUTHN)
    , m_authenticatorCoordinator(makeUniqueRef<AuthenticatorCoordinator>(WTFMove(pageConfiguration.authenticatorCoordinatorClient)))
#endif
#if HAVE(DIGITAL_CREDENTIALS_UI)
    , m_credentialRequestCoordinator(CredentialRequestCoordinator::create(WTFMove(pageConfiguration.credentialRequestCoordinatorClient), *this))
#endif
#if ENABLE(APPLICATION_MANIFEST)
    , m_applicationManifest(pageConfiguration.applicationManifest)
#endif
#if ENABLE(DEVICE_ORIENTATION) && PLATFORM(IOS_FAMILY)
    , m_deviceOrientationUpdateProvider(WTFMove(pageConfiguration.deviceOrientationUpdateProvider))
#endif
    , m_corsDisablingPatterns(WTFMove(pageConfiguration.corsDisablingPatterns))
    , m_maskedURLSchemes(WTFMove(pageConfiguration.maskedURLSchemes))
    , m_allowedNetworkHosts(WTFMove(pageConfiguration.allowedNetworkHosts))
    , m_loadsSubresources(pageConfiguration.loadsSubresources)
    , m_shouldRelaxThirdPartyCookieBlocking(pageConfiguration.shouldRelaxThirdPartyCookieBlocking)
    , m_fixedContainerEdgesAndElements(std::make_pair(makeUniqueRef<FixedContainerEdges>(), WeakElementEdges { }))
    , m_httpsUpgradeEnabled(pageConfiguration.httpsUpgradeEnabled)
    , m_portsForUpgradingInsecureSchemeForTesting(WTFMove(pageConfiguration.portsForUpgradingInsecureSchemeForTesting))
    , m_storageProvider(WTFMove(pageConfiguration.storageProvider))
    , m_modelPlayerProvider(WTFMove(pageConfiguration.modelPlayerProvider))
#if ENABLE(ATTACHMENT_ELEMENT)
    , m_attachmentElementClient(WTFMove(pageConfiguration.attachmentElementClient))
#endif
    , m_opportunisticTaskScheduler(OpportunisticTaskScheduler::create(*this))
    , m_contentSecurityPolicyModeForExtension(WTFMove(pageConfiguration.contentSecurityPolicyModeForExtension))
    , m_badgeClient(WTFMove(pageConfiguration.badgeClient))
    , m_historyItemClient(WTFMove(pageConfiguration.historyItemClient))
#if PLATFORM(VISION) && ENABLE(GAMEPAD)
    , m_gamepadAccessRequiresExplicitConsent(pageConfiguration.gamepadAccessRequiresExplicitConsent)
#endif
#if ENABLE(WRITING_TOOLS)
    , m_writingToolsController(makeUniqueRef<WritingToolsController>(*this))
#endif
    , m_activeNowPlayingSessionUpdateTimer(*this, &Page::updateActiveNowPlayingSessionNow)
    , m_topDocumentSyncData(DocumentSyncData::create())
#if HAVE(AUDIT_TOKEN)
    , m_presentingApplicationAuditToken(WTFMove(pageConfiguration.presentingApplicationAuditToken))
#endif
#if PLATFORM(COCOA)
    , m_presentingApplicationBundleIdentifier(WTFMove(pageConfiguration.presentingApplicationBundleIdentifier))
#endif
    , m_mediaSessionManagerFactory(WTFMove(pageConfiguration.mediaSessionManagerFactory))
{
    updateTimerThrottlingState();

    protectedPluginInfoProvider()->addPage(*this);
    protectedUserContentProvider()->addPage(*this);
    protectedVisitedLinkStore()->addPage(*this);

    static bool firstTimeInitializationRan = false;
    if (!firstTimeInitializationRan) {
        firstTimeInitialization();
        firstTimeInitializationRan = true;
    }

    ASSERT(!allPages().contains(*this));
    allPages().add(*this);

    if (!isUtilityPage()) {
        ++gNonUtilityPageCount;
        MemoryPressureHandler::setPageCount(gNonUtilityPageCount);
    }

#ifndef NDEBUG
    pageCounter.increment();
#endif

    protectedStorageNamespaceProvider()->setSessionStorageQuota(m_settings->sessionStorageQuota());

#if ENABLE(REMOTE_INSPECTOR)
    if (m_inspectorController->inspectorBackendClient() && m_inspectorController->inspectorBackendClient()->allowRemoteInspectionToPageDirectly())
        m_inspectorDebuggable->init();
#endif

#if PLATFORM(COCOA)
    platformInitialize();
#endif

#if PLATFORM(VISION) && ENABLE(GAMEPAD)
    initializeGamepadAccessForPageLoad();
#endif

#if HAVE(SUPPORT_HDR_DISPLAY)
    updateDisplayEDRHeadroom();
#endif

    settingsDidChange();

    if (m_lowPowerModeNotifier->isLowPowerModeEnabled())
        m_throttlingReasons.add(ThrottlingReason::LowPowerMode);

    if (m_thermalMitigationNotifier->thermalMitigationEnabled()) {
        m_throttlingReasons.add(ThrottlingReason::ThermalMitigation);
        m_throttlingReasons.set(ThrottlingReason::AggressiveThermalMitigation, settings().respondToThermalPressureAggressively());
    }
}

Page::~Page()
{
    m_validationMessageClient = nullptr;
    m_diagnosticLoggingClient = nullptr;
    m_performanceLoggingClient = nullptr;
    protectedMainFrame()->disconnectView();
    setGroupName(String());
    allPages().remove(*this);
    if (!isUtilityPage()) {
        --gNonUtilityPageCount;
        MemoryPressureHandler::setPageCount(gNonUtilityPageCount);
    }

    m_inspectorController->inspectedPageDestroyed();
#if ENABLE(REMOTE_INSPECTOR)
    m_inspectorDebuggable->detachFromPage();
#endif

    forEachLocalFrame([] (LocalFrame& frame) {
        frame.willDetachPage();
        frame.detachFromPage();
    });
    ASSERT(m_rootFrames.isEmpty());

    if (RefPtr scrollingCoordinator = m_scrollingCoordinator)
        scrollingCoordinator->pageDestroyed();

#if ENABLE(RESOURCE_USAGE)
    if (RefPtr resourceUsageOverlay = m_resourceUsageOverlay)
        resourceUsageOverlay->detachFromPage();
#endif

    checkedBackForward()->close();
    if (!isUtilityPage())
        BackForwardCache::singleton().removeAllItemsForPage(*this);

#ifndef NDEBUG
    pageCounter.decrement();
#endif

    protectedPluginInfoProvider()->removePage(*this);
    protectedUserContentProvider()->removePage(*this);
    protectedVisitedLinkStore()->removePage(*this);
}

CheckedRef<BackForwardController> Page::checkedBackForward()
{
    return m_backForwardController.get();
}

void Page::firstTimeInitialization()
{
    platformStrategies()->loaderStrategy()->addOnlineStateChangeListener(&networkStateChanged);

    FontCache::registerFontCacheInvalidationCallback([] {
        updateStyleForAllPagesAfterGlobalChangeInEnvironment();
    });
}

void Page::clearPreviousItemFromAllPages(BackForwardItemIdentifier itemID)
{
    for (auto& page : allPages()) {
        RefPtr localMainFrame = page->localMainFrame();
        if (!localMainFrame)
            return;

        Ref controller = localMainFrame->loader().history();
        if (controller->previousItem() && controller->previousItem()->itemID() == itemID) {
            controller->clearPreviousItem();
            return;
        }
    }
}

uint64_t Page::renderTreeSize() const
{
    uint64_t total = 0;
    forEachDocument([&] (Document& document) {
        if (CheckedPtr renderView = document.renderView())
            total += renderView->rendererCount();
    });
    return total;
}

void Page::destroyRenderTrees()
{
    // When closing or entering back/forward cache, tear down the render tree before setting the in-cache flag.
    // This maintains the invariant that render trees are never present in the back/forward cache or outliving the page.
    // Note that destruction happens bottom-up so that the main frame's tree dies last.
    for (RefPtr frame = m_mainFrame->tree().traversePrevious(CanWrap::Yes); frame; frame = frame->tree().traversePrevious(CanWrap::No)) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        if (!localFrame->document())
            continue;
        Ref document = *localFrame->document();
        if (document->hasLivingRenderTree())
            document->destroyRenderTree();
    }
}

OptionSet<DisabledAdaptations> Page::disabledAdaptations() const
{
    if (RefPtr localTopDocument = this->localTopDocument())
        return localTopDocument->disabledAdaptations();
    return { };
}

static RefPtr<Document> viewportDocumentForFrame(const Frame& frame)
{
    RefPtr localFrame = dynamicDowncast<LocalFrame>(frame);
    if (!localFrame)
        return nullptr;

    RefPtr document = localFrame->document();
    if (!document)
        return nullptr;

    RefPtr page = localFrame->page();
    if (!page)
        return nullptr;

    if (RefPtr fullscreenDocument = page->outermostFullscreenDocument())
        return fullscreenDocument;

    return document;
}

ViewportArguments Page::viewportArguments() const
{
    if (RefPtr document = viewportDocumentForFrame(protectedMainFrame()))
        return document->viewportArguments();
    return ViewportArguments();
}

void Page::setOverrideViewportArguments(const std::optional<ViewportArguments>& viewportArguments)
{
    std::optional<ViewportArguments> oldArguments = m_overrideViewportArguments ? std::optional(*m_overrideViewportArguments) : std::nullopt;
    if (oldArguments == viewportArguments)
        return;
    m_overrideViewportArguments = viewportArguments ? makeUnique<ViewportArguments>(*viewportArguments) : nullptr;
    if (RefPtr localTopDocument = this->localTopDocument())
        localTopDocument->updateViewportArguments();
}

ScrollingCoordinator* Page::scrollingCoordinator()
{
    if (!m_scrollingCoordinator && m_settings->scrollingCoordinatorEnabled()) {
        m_scrollingCoordinator = chrome().client().createScrollingCoordinator(*this);
        if (!m_scrollingCoordinator)
            m_scrollingCoordinator = ScrollingCoordinator::create(this);

        protectedScrollingCoordinator()->windowScreenDidChange(m_displayID, m_displayNominalFramesPerSecond);
    }

    return m_scrollingCoordinator.get();
}

RefPtr<ScrollingCoordinator> Page::protectedScrollingCoordinator()
{
    return scrollingCoordinator();
}

String Page::scrollingStateTreeAsText()
{
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr) {
        if (RefPtr frameView = document->view())
            frameView->updateLayoutAndStyleIfNeededRecursive(LayoutOptions::UpdateCompositingLayers);
#if ENABLE(IOS_TOUCH_EVENTS)
        document->updateTouchEventRegions();
#endif
    }

    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        return scrollingCoordinator->scrollingStateTreeAsText();

    return String();
}

String Page::synchronousScrollingReasonsAsText()
{
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr)
        document->updateLayout();

    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        return scrollingCoordinator->synchronousScrollingReasonsAsText();

    return { };
}

Ref<DOMRectList> Page::nonFastScrollableRectsForTesting()
{
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr) {
        document->updateLayout();
#if ENABLE(IOS_TOUCH_EVENTS)
        document->updateTouchEventRegions();
#endif
    }

    Vector<IntRect> rects;
    if (RefPtr scrollingCoordinator = this->scrollingCoordinator()) {
        const EventTrackingRegions& eventTrackingRegions = scrollingCoordinator->absoluteEventTrackingRegions();
        for (const auto& synchronousEventRegion : eventTrackingRegions.eventSpecificSynchronousDispatchRegions)
            rects.appendVector(synchronousEventRegion.value.rects());
    }

    Vector<FloatQuad> quads(rects.size());
    for (size_t i = 0; i < rects.size(); ++i)
        quads[i] = FloatRect(rects[i]);

    return DOMRectList::create(quads);
}

Ref<DOMRectList> Page::touchEventRectsForEventForTesting(EventTrackingRegions::EventType eventType)
{
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr) {
        document->updateLayout();
#if ENABLE(IOS_TOUCH_EVENTS)
        document->updateTouchEventRegions();
#endif
    }

    Vector<IntRect> rects;
    if (RefPtr scrollingCoordinator = this->scrollingCoordinator()) {
        const EventTrackingRegions& eventTrackingRegions = scrollingCoordinator->absoluteEventTrackingRegions();
        const auto& region = eventTrackingRegions.eventSpecificSynchronousDispatchRegions.get(eventType);
        rects.appendVector(region.rects());
    }

    Vector<FloatQuad> quads(rects.size());
    for (size_t i = 0; i < rects.size(); ++i)
        quads[i] = FloatRect(rects[i]);

    return DOMRectList::create(quads);
}

Ref<DOMRectList> Page::passiveTouchEventListenerRectsForTesting()
{
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr) {
        document->updateLayout();
#if ENABLE(IOS_TOUCH_EVENTS)
        document->updateTouchEventRegions();
#endif  
    }

    Vector<IntRect> rects;
    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        rects.appendVector(scrollingCoordinator->absoluteEventTrackingRegions().asynchronousDispatchRegion.rects());

    Vector<FloatQuad> quads(rects.size());
    for (size_t i = 0; i < rects.size(); ++i)
        quads[i] = FloatRect(rects[i]);

    return DOMRectList::create(quads);
}

void Page::settingsDidChange()
{
#if ENABLE(WEB_RTC)
    m_webRTCProvider->setH265Support(settings().webRTCH265CodecEnabled());
    m_webRTCProvider->setVP9Support(settings().webRTCVP9Profile0CodecEnabled(), settings().webRTCVP9Profile2CodecEnabled());
    m_webRTCProvider->setAV1Support(settings().webRTCAV1CodecEnabled());
    m_webRTCProvider->setPortAllocatorRange(settings().webRTCUDPPortRange());
#endif
}

std::optional<AXTreeData> Page::accessibilityTreeData(IncludeDOMInfo includeDOMInfo) const
{
    RefPtr localTopDocument = this->localTopDocument();
    if (!localTopDocument)
        return std::nullopt;

    if (CheckedPtr cache = localTopDocument->existingAXObjectCache())
        return { includeDOMInfo == IncludeDOMInfo::Yes ? cache->treeData({ { AXStreamOptions::IdentifierAttribute, AXStreamOptions::OuterHTML, AXStreamOptions::RendererOrNode } }) : cache->treeData() };
    return std::nullopt;
}

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
void Page::clearAccessibilityIsolatedTree()
{
    if (CheckedPtr cache = axObjectCache()) {
        if (std::optional identifier = this->identifier())
            AXIsolatedTree::removeTreeForPageID(*identifier);
    }
}
#endif // ENABLE(ACCESSIBILITY_ISOLATED_TREE)

void Page::progressEstimateChanged(LocalFrame& frameWithProgressUpdate) const
{
    if (RefPtr document = frameWithProgressUpdate.document()) {
        if (CheckedPtr axObjectCache = document->existingAXObjectCache())
            axObjectCache->updateLoadingProgress(progress().estimatedProgress());
    }
}

void Page::progressFinished(LocalFrame& frameWithCompletedProgress) const
{
    if (RefPtr document = frameWithCompletedProgress.document()) {
        if (CheckedPtr axObjectCache = document->existingAXObjectCache())
            axObjectCache->loadingFinished();
    }
}

void Page::setMainFrame(Ref<Frame>&& frame)
{
    m_mainFrame = WTFMove(frame);

    RefPtr<Document> document;
    if (RefPtr localFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get()))
        document = localFrame->document();

    m_topDocumentSyncData = document ? document->syncData() : DocumentSyncData::create();
}

void Page::setMainFrameURLAndOrigin(const URL& url, RefPtr<SecurityOrigin>&& origin)
{
    // This URL and SecurityOrigin is relevant to this Page only if it is not
    // directly hosting the local main frame.
    RefPtr localFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (!localFrame) {
        m_topDocumentSyncData->documentURL = url;

        if (!origin)
            origin = SecurityOrigin::create(url);
        m_topDocumentSyncData->documentSecurityOrigin = WTFMove(origin);

        return;
    }

    if (!settings().siteIsolationEnabled())
        return;

    // If this page is hosting the local main frame, make sure the url and origin
    // match what we expect, then broadcast them out to other processes.
    RELEASE_ASSERT(url == m_topDocumentSyncData->documentURL);
    if (!origin)
        RELEASE_ASSERT(!m_topDocumentSyncData->documentSecurityOrigin);

    processSyncClient().broadcastTopDocumentSyncDataToOtherProcesses(m_topDocumentSyncData.get());
}

void Page::setIsClosing()
{
    m_topDocumentSyncData->isClosing = true;
    if (settings().siteIsolationEnabled())
        processSyncClient().broadcastIsClosingToOtherProcesses(true);
}

bool Page::isClosing() const
{
    return m_topDocumentSyncData->isClosing;
}

#if ENABLE(DOM_AUDIO_SESSION)
void Page::setAudioSessionType(DOMAudioSessionType audioSessionType)
{
    m_topDocumentSyncData->audioSessionType = audioSessionType;
    if (settings().siteIsolationEnabled())
        processSyncClient().broadcastAudioSessionTypeToOtherProcesses(audioSessionType);
}

DOMAudioSessionType Page::audioSessionType() const
{
    return m_topDocumentSyncData->audioSessionType;
}
#endif

void Page::setUserDidInteractWithPage(bool didInteract)
{
    if (m_topDocumentSyncData->userDidInteractWithPage == didInteract)
        return;

    m_topDocumentSyncData->userDidInteractWithPage = didInteract;
    if (settings().siteIsolationEnabled())
        processSyncClient().broadcastUserDidInteractWithPageToOtherProcesses(didInteract);
}

bool Page::userDidInteractWithPage() const
{
    return m_topDocumentSyncData->userDidInteractWithPage;
}

void Page::setAutofocusProcessed()
{
    if (m_topDocumentSyncData->isAutofocusProcessed)
        return;

    m_topDocumentSyncData->isAutofocusProcessed = true;
    if (settings().siteIsolationEnabled())
        processSyncClient().broadcastIsAutofocusProcessedToOtherProcesses(true);
}

bool Page::autofocusProcessed() const
{
    return m_topDocumentSyncData->isAutofocusProcessed;
}

bool Page::topDocumentHasDocumentClass(DocumentClass documentClass) const
{
    return m_topDocumentSyncData->documentClasses.contains(documentClass);
}

bool Page::hasInjectedUserScript()
{
    return m_topDocumentSyncData->hasInjectedUserScript;
}

void Page::setHasInjectedUserScript()
{
    if (m_topDocumentSyncData->hasInjectedUserScript)
        return;

    m_topDocumentSyncData->hasInjectedUserScript = true;
    if (settings().siteIsolationEnabled())
        processSyncClient().broadcastHasInjectedUserScriptToOtherProcesses(true);
}

void Page::updateProcessSyncData(const ProcessSyncData& data)
{
    switch (data.type) {
    case ProcessSyncDataType::DocumentClasses:
    case ProcessSyncDataType::DocumentSecurityOrigin:
    case ProcessSyncDataType::DocumentURL:
    case ProcessSyncDataType::HasInjectedUserScript:
    case ProcessSyncDataType::IsAutofocusProcessed:
    case ProcessSyncDataType::IsClosing:
    case ProcessSyncDataType::UserDidInteractWithPage:
#if ENABLE(DOM_AUDIO_SESSION)
    case ProcessSyncDataType::AudioSessionType:
#endif
        protectedTopDocumentSyncData()->update(data);
        break;
    case ProcessSyncDataType::FrameCanCreatePaymentSession:
    case ProcessSyncDataType::FrameDocumentSecurityOrigin:
        ASSERT_NOT_REACHED();
    }
}

void Page::updateTopDocumentSyncData(Ref<DocumentSyncData>&& data)
{
    if (RefPtr localFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get())) {
        // Prefer the main LocalFrame document's data, but if the main LocalFrame
        // has no document, accept the remote pushed data.
        if (localFrame->document())
            return;
    }

    m_topDocumentSyncData = WTFMove(data);
}

void Page::setMainFrameURLFragment(String&& fragment)
{
    if (!fragment.isEmpty())
        m_mainFrameURLFragment = WTFMove(fragment);
}

const URL& Page::mainFrameURL() const
{
    return m_topDocumentSyncData->documentURL;
}

SecurityOrigin& Page::mainFrameOrigin() const
{
    if (!m_topDocumentSyncData->documentSecurityOrigin)
        return SecurityOrigin::opaqueOrigin();
    return *m_topDocumentSyncData->documentSecurityOrigin;
}

bool Page::openedByDOM() const
{
    return m_openedByDOM;
}

void Page::setOpenedByDOM()
{
    m_openedByDOM = true;
}

void Page::goToItem(LocalFrame& frame, HistoryItem& item, FrameLoadType type, ShouldTreatAsContinuingLoad shouldTreatAsContinuingLoad, ProcessSwapDisposition processSwapDisposition)
{
    // stopAllLoaders may end up running onload handlers, which could cause further history traversals that may lead to the passed in HistoryItem
    // being deref()-ed. Make sure we can still use it with HistoryController::goToItem later.
    Ref protectedItem { item };

    if (frame.loader().history().shouldStopLoadingForHistoryItem(item))
        frame.loader().stopAllLoadersAndCheckCompleteness();
    frame.loader().history().goToItem(item, type, shouldTreatAsContinuingLoad, processSwapDisposition);
}

void Page::goToItemForNavigationAPI(LocalFrame& frame, HistoryItem& item, FrameLoadType type, LocalFrame& triggeringFrame, NavigationAPIMethodTracker* tracker)
{
    if (frame.loader().history().shouldStopLoadingForHistoryItem(item))
        frame.loader().stopAllLoadersAndCheckCompleteness();
    frame.loader().history().goToItemForNavigationAPI(item, type, triggeringFrame, tracker);
}

void Page::setGroupName(const String& name)
{
    if (m_group && !m_group->name().isEmpty()) {
        ASSERT(m_group != m_singlePageGroup.get());
        ASSERT(!m_singlePageGroup);
        m_group->removePage(*this);
    }

    if (name.isEmpty())
        m_group = m_singlePageGroup.get();
    else {
        m_singlePageGroup = nullptr;
        m_group = PageGroup::pageGroup(name);
        m_group->addPage(*this);
    }
}

const String& Page::groupName() const
{
    return m_group ? m_group->name() : nullAtom().string();
}

Ref<BroadcastChannelRegistry> Page::protectedBroadcastChannelRegistry() const
{
    return m_broadcastChannelRegistry;
}

void Page::setBroadcastChannelRegistry(Ref<BroadcastChannelRegistry>&& broadcastChannelRegistry)
{
    m_broadcastChannelRegistry = WTFMove(broadcastChannelRegistry);
}

void Page::initGroup()
{
    ASSERT(!m_singlePageGroup);
    ASSERT(!m_group);
    m_singlePageGroup = makeUnique<PageGroup>(*this);
    m_group = m_singlePageGroup.get();
}

void Page::updateStyleAfterChangeInEnvironment()
{
    forEachDocument([] (Document& document) {
        if (RefPtr styleResolver = document.styleScope().resolverIfExists())
            styleResolver->invalidateMatchedDeclarationsCache();
        document.scheduleFullStyleRebuild();
        document.styleScope().didChangeStyleSheetEnvironment();
        document.updateElementsAffectedByMediaQueries();
        document.scheduleRenderingUpdate(RenderingUpdateStep::MediaQueryEvaluation);
    });
}

void Page::updateStyleForAllPagesAfterGlobalChangeInEnvironment()
{
    for (auto& page : allPages())
        Ref { page.get() }->updateStyleAfterChangeInEnvironment();
}

void Page::setNeedsRecalcStyleInAllFrames()
{
    // FIXME: Figure out what this function is actually trying to add in different call sites.
    forEachDocument([] (Document& document) {
        document.styleScope().didChangeStyleSheetEnvironment();
    });
}

void Page::refreshPlugins(bool reload)
{
    WeakHashSet<PluginInfoProvider> pluginInfoProviders;

    for (auto& page : allPages())
        pluginInfoProviders.add(Ref { page.get() }->protectedPluginInfoProvider());

    for (Ref pluginInfoProvider : pluginInfoProviders)
        pluginInfoProvider->refresh(reload);
}

PluginData& Page::pluginData()
{
    if (!m_pluginData)
        m_pluginData = PluginData::create(*this);
    return *m_pluginData;
}

Ref<PluginData> Page::protectedPluginData()
{
    return pluginData();
}

void Page::clearPluginData()
{
    m_pluginData = nullptr;
}

bool Page::showAllPlugins() const
{
    if (m_showAllPlugins)
        return true;

    return mainFrameOrigin().isLocal();
}

inline std::optional<std::pair<WeakRef<MediaCanStartListener>, WeakRef<Document, WeakPtrImplWithEventTargetData>>>  Page::takeAnyMediaCanStartListener()
{
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        RefPtr document = localFrame->document();
        if (!document)
            continue;
        if (RefPtr listener = document->takeAnyMediaCanStartListener())
            return { { *listener, *document } };
    }
    return std::nullopt;
}

void Page::setCanStartMedia(bool canStartMedia)
{
    if (m_canStartMedia == canStartMedia)
        return;

    m_canStartMedia = canStartMedia;

    while (m_canStartMedia) {
        auto listener = takeAnyMediaCanStartListener();
        if (!listener)
            break;
        Ref { listener->first.get() }->mediaCanStart(Ref { listener->second.get() });
    }
}

Ref<Frame> Page::protectedMainFrame() const
{
    return m_mainFrame;
}

static Frame* incrementFrame(Frame* current, bool forward, CanWrap canWrap, DidWrap* didWrap = nullptr)
{
    return forward
        ? current->tree().traverseNext(canWrap, didWrap)
        : current->tree().traversePrevious(canWrap, didWrap);
}

std::optional<FrameIdentifier> Page::findString(const String& target, FindOptions options, DidWrap* didWrap)
{
    if (target.isEmpty())
        return std::nullopt;

    CanWrap canWrap = options.contains(FindOption::WrapAround) ? CanWrap::Yes : CanWrap::No;
    RefPtr frame = m_focusController->focusedFrame() ? m_focusController->focusedFrame() : m_mainFrame.ptr();
    RefPtr startFrame = frame;
    RefPtr focusedLocalFrame = dynamicDowncast<LocalFrame>(frame);
    do {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame.get());
        if (!localFrame) {
            frame = incrementFrame(frame.get(), !options.contains(FindOption::Backwards), canWrap, didWrap);
            continue;
        }
        if (localFrame->protectedEditor()->findString(target, (options - FindOption::WrapAround) | FindOption::StartInSelection)) {
            if (!options.contains(FindOption::DoNotSetSelection)) {
                if (focusedLocalFrame && localFrame != focusedLocalFrame)
                    focusedLocalFrame->checkedSelection()->clear();
                m_focusController->setFocusedFrame(localFrame.get());
            }
            return localFrame->frameID();
        }
        frame = incrementFrame(frame.get(), !options.contains(FindOption::Backwards), canWrap, didWrap);
    } while (frame && frame != startFrame);

    // Search contents of startFrame, on the other side of the selection that we did earlier.
    // We cheat a bit and just research with wrap on
    if (canWrap == CanWrap::Yes && focusedLocalFrame && !focusedLocalFrame->selection().isNone()) {
        if (didWrap)
            *didWrap = DidWrap::Yes;
        bool found = focusedLocalFrame->protectedEditor()->findString(target, options | FindOption::WrapAround | FindOption::StartInSelection);
        if (!options.contains(FindOption::DoNotSetSelection))
            m_focusController->setFocusedFrame(frame.get());
        return found ? std::make_optional(focusedLocalFrame->frameID()) : std::nullopt;
    }

    return std::nullopt;
}

#if ENABLE(IMAGE_ANALYSIS)
void Page::analyzeImagesForFindInPage(Function<void()>&& callback)
{
    if (settings().imageAnalysisDuringFindInPageEnabled()) {
        Ref imageAnalysisQueue = this->imageAnalysisQueue();
        imageAnalysisQueue->setDidBecomeEmptyCallback(WTFMove(callback));
        RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
        if (RefPtr mainDocument = localMainFrame ? localMainFrame->document() : nullptr)
            imageAnalysisQueue->enqueueAllImagesIfNeeded(*mainDocument, { }, { });
    }
}
#endif

auto Page::findTextMatches(const String& target, FindOptions options, unsigned limit, bool markMatches) -> MatchingRanges
{
    MatchingRanges result;

    RefPtr frame { &mainFrame() };
    RefPtr<LocalFrame> frameWithSelection;
    do {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame.get());
        if (!localFrame) {
            frame = incrementFrame(frame.get(), true, CanWrap::No);
            continue;
        }
        localFrame->protectedEditor()->countMatchesForText(target, { }, options, limit ? (limit - result.ranges.size()) : 0, markMatches, &result.ranges);
        if (localFrame->selection().isRange())
            frameWithSelection = localFrame;
        frame = incrementFrame(frame.get(), true, CanWrap::No);
    } while (frame);

    if (result.ranges.isEmpty())
        return result;

    if (frameWithSelection) {
        result.indexForSelection = NoMatchAfterUserSelection;
        auto selectedRange = *frameWithSelection->selection().selection().firstRange();
        if (options.contains(FindOption::Backwards)) {
            for (size_t i = result.ranges.size(); i > 0; --i) {
                // FIXME: Seems like this should be is_gteq to correctly handle the same string found twice in a row.
                if (is_gt(treeOrder<ComposedTree>(selectedRange.start, result.ranges[i - 1].end))) {
                    result.indexForSelection = i - 1;
                    break;
                }
            }
        } else {
            for (size_t i = 0, size = result.ranges.size(); i < size; ++i) {
                // FIXME: Seems like this should be is_lteq to correctly handle the same string found twice in a row.
                if (is_lt(treeOrder<ComposedTree>(selectedRange.end, result.ranges[i].start))) {
                    result.indexForSelection = i;
                    break;
                }
            }
        }
    } else {
        if (options.contains(FindOption::Backwards))
            result.indexForSelection = result.ranges.size() - 1;
        else
            result.indexForSelection = 0;
    }

    return result;
}

std::optional<SimpleRange> Page::rangeOfString(const String& target, const std::optional<SimpleRange>& referenceRange, FindOptions options)
{
    if (target.isEmpty())
        return std::nullopt;

    if (referenceRange && referenceRange->start.document().page() != this)
        return std::nullopt;

    CanWrap canWrap = options.contains(FindOption::WrapAround) ? CanWrap::Yes : CanWrap::No;
    RefPtr frame = referenceRange ? referenceRange->start.document().frame() : &mainFrame();
    RefPtr startFrame = dynamicDowncast<LocalFrame>(frame.get());
    do {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame.get());
        if (!localFrame) {
            frame = incrementFrame(frame.get(), !options.contains(FindOption::Backwards), canWrap);
            continue;
        }
        if (auto resultRange = localFrame->protectedEditor()->rangeOfString(target, localFrame.get() == startFrame.get() ? referenceRange : std::nullopt, options - FindOption::WrapAround))
            return resultRange;
        frame = incrementFrame(localFrame.get(), !options.contains(FindOption::Backwards), canWrap);
    } while (frame && frame != startFrame);

    // Search contents of startFrame, on the other side of the reference range that we did earlier.
    // We cheat a bit and just search again with wrap on.
    if (canWrap == CanWrap::Yes && referenceRange) {
        if (auto resultRange = startFrame->protectedEditor()->rangeOfString(target, *referenceRange, options | FindOption::WrapAround | FindOption::StartInSelection))
            return resultRange;
    }

    return std::nullopt;
}

unsigned Page::findMatchesForText(const String& target, FindOptions options, unsigned maxMatchCount, ShouldHighlightMatches shouldHighlightMatches, ShouldMarkMatches shouldMarkMatches)
{
    if (target.isEmpty())
        return 0;

    unsigned matchCount = 0;

    RefPtr frame = mainFrame();
    do {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame.get());
        if (!localFrame) {
            frame = incrementFrame(frame.get(), true, CanWrap::No);
            continue;
        }
        if (shouldMarkMatches == MarkMatches)
            localFrame->protectedEditor()->setMarkedTextMatchesAreHighlighted(shouldHighlightMatches == HighlightMatches);
        matchCount += localFrame->protectedEditor()->countMatchesForText(target, std::nullopt, options, maxMatchCount ? (maxMatchCount - matchCount) : 0, shouldMarkMatches == MarkMatches, nullptr);
        frame = incrementFrame(frame.get(), true, CanWrap::No);
    } while (frame);

    return matchCount;
}

unsigned Page::markAllMatchesForText(const String& target, FindOptions options, bool shouldHighlight, unsigned maxMatchCount)
{
    return findMatchesForText(target, options, maxMatchCount, shouldHighlight ? HighlightMatches : DoNotHighlightMatches, MarkMatches);
}

unsigned Page::countFindMatches(const String& target, FindOptions options, unsigned maxMatchCount)
{
    return findMatchesForText(target, options, maxMatchCount, DoNotHighlightMatches, DoNotMarkMatches);
}

struct FindReplacementRange {
    RefPtr<ContainerNode> root;
    CharacterRange range;
};

static void replaceRanges(Page& page, const Vector<FindReplacementRange>& ranges, const String& replacementText)
{
    HashMap<RefPtr<ContainerNode>, Vector<FindReplacementRange>> rangesByContainerNode;
    for (auto& range : ranges) {
        auto& rangeList = rangesByContainerNode.ensure(range.root, [] {
            return Vector<FindReplacementRange> { };
        }).iterator->value;

        // Ensure that ranges are sorted by their end offsets, per editing container.
        auto endOffsetForRange = range.range.location + range.range.length;
        auto insertionIndex = rangeList.size();
        for (auto iterator = rangeList.rbegin(); iterator != rangeList.rend(); ++iterator) {
            auto endOffsetBeforeInsertionIndex = iterator->range.location + iterator->range.length;
            if (endOffsetForRange >= endOffsetBeforeInsertionIndex)
                break;
            insertionIndex--;
        }
        rangeList.insert(insertionIndex, range);
    }

    HashMap<RefPtr<LocalFrame>, unsigned> frameToTraversalIndexMap;
    unsigned currentFrameTraversalIndex = 0;
    for (RefPtr frame = page.mainFrame(); frame; frame = frame->tree().traverseNext()) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame))
            frameToTraversalIndexMap.set(WTFMove(localFrame), currentFrameTraversalIndex++);
    }

    // Likewise, iterate backwards (in document and frame order) through editing containers that contain text matches,
    // so that we're consistent with our backwards iteration behavior per editing container when replacing text.
    auto containerNodesInOrderOfReplacement = copyToVector(rangesByContainerNode.keys());
    std::ranges::sort(containerNodesInOrderOfReplacement, [frameToTraversalIndexMap](auto& firstNode, auto& secondNode) {
        if (firstNode == secondNode)
            return false;

        RefPtr firstFrame = firstNode->document().frame();
        if (!firstFrame)
            return true;

        RefPtr secondFrame = secondNode->document().frame();
        if (!secondFrame)
            return false;

        if (firstFrame == secondFrame) {
            // Must not use Node::compareDocumentPosition here because some editing roots are inside shadow roots.
            return is_gt(treeOrder<ComposedTree>(*firstNode, *secondNode));
        }

        return frameToTraversalIndexMap.get(firstFrame) > frameToTraversalIndexMap.get(secondFrame);
    });

    for (auto& container : containerNodesInOrderOfReplacement) {
        RefPtr frame = container->document().frame();
        if (!frame)
            continue;

        // Iterate backwards through ranges when replacing text, such that earlier text replacements don't clobber replacement ranges later on.
        auto& ranges = rangesByContainerNode.find(container)->value;
        for (auto iterator = ranges.rbegin(); iterator != ranges.rend(); ++iterator) {
            auto range = resolveCharacterRange(makeRangeSelectingNodeContents(*container), iterator->range);
            if (range.collapsed())
                continue;

            frame->checkedSelection()->setSelectedRange(range, Affinity::Downstream, FrameSelection::ShouldCloseTyping::Yes);
            frame->protectedEditor()->replaceSelectionWithText(replacementText, Editor::SelectReplacement::Yes, Editor::SmartReplace::No, EditAction::InsertReplacement);
        }
    }
}

uint32_t Page::replaceRangesWithText(const Vector<SimpleRange>& rangesToReplace, const String& replacementText, bool /*selectionOnly*/)
{
    // FIXME: In the future, we should respect the `selectionOnly` flag by checking whether each range being replaced is contained within its frame's selection.

    auto replacementRanges = WTF::compactMap(rangesToReplace, [&](auto& range) -> std::optional<FindReplacementRange> {
        RefPtr highestRoot = highestEditableRoot(makeDeprecatedLegacyPosition(range.start));
        if (!highestRoot || highestRoot != highestEditableRoot(makeDeprecatedLegacyPosition(range.end)) || !highestRoot->document().frame())
            return std::nullopt;
        auto scope = makeRangeSelectingNodeContents(*highestRoot);
        return FindReplacementRange { WTFMove(highestRoot), characterRange(scope, range) };
    });

    replaceRanges(*this, replacementRanges, replacementText);
    return rangesToReplace.size();
}

uint32_t Page::replaceSelectionWithText(const String& replacementText)
{
    RefPtr frame = focusController().focusedOrMainFrame();
    if (!frame)
        return 0;

    auto selection = frame->selection().selection();
    if (!selection.isContentEditable())
        return 0;

    auto editAction = selection.isRange() ? EditAction::InsertReplacement : EditAction::Insert;
    frame->protectedEditor()->replaceSelectionWithText(replacementText, Editor::SelectReplacement::Yes, Editor::SmartReplace::No, editAction);
    return 1;
}

void Page::unmarkAllTextMatches()
{
    forEachDocument([] (Document& document) {
        if (CheckedPtr markers = document.markersIfExists())
            markers->removeMarkers(DocumentMarkerType::TextMatch);
    });
}

#if ENABLE(EDITABLE_REGION)

void Page::setEditableRegionEnabled(bool enabled)
{
    if (m_isEditableRegionEnabled == enabled)
        return;
    m_isEditableRegionEnabled = enabled;
    RefPtr localMainFrame = this->localMainFrame();
    RefPtr frameView = localMainFrame ? localMainFrame->view() : nullptr;
    if (!frameView)
        return;
    if (CheckedPtr renderView = frameView->renderView())
        renderView->compositor().invalidateEventRegionForAllLayers();
}

#endif

#if ENABLE(EDITABLE_REGION)

bool Page::shouldBuildEditableRegion() const
{
    return m_isEditableRegionEnabled || OptionSet<DebugOverlayRegions>::fromRaw(m_settings->visibleDebugOverlayRegions()).contains(DebugOverlayRegions::EditableElementRegion);
}

#endif

Vector<Ref<Element>> Page::editableElementsInRect(const FloatRect& searchRectInRootViewCoordinates) const
{
    RefPtr localMainFrame = this->localMainFrame();
    RefPtr frameView = localMainFrame ? localMainFrame->view() : nullptr;
    if (!frameView)
        return { };

    RefPtr document = localMainFrame->document();
    if (!document)
        return { };

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::CollectMultipleElements, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
    LayoutRect searchRectInMainFrameCoordinates = frameView->rootViewToContents(roundedIntRect(searchRectInRootViewCoordinates));
    HitTestResult hitTestResult { searchRectInMainFrameCoordinates };
    if (!document->hitTest(hitType, hitTestResult))
        return { };

    auto rootEditableElement = [](Node& node) -> Element* {
        if (RefPtr element = dynamicDowncast<HTMLTextFormControlElement>(node)) {
            if (element->isInnerTextElementEditable())
                return &uncheckedDowncast<Element>(node);
        } else if (is<Element>(node) && node.hasEditableStyle())
            return node.rootEditableElement();
        return nullptr;
    };

    ListHashSet<Ref<Element>> rootEditableElements;
    auto& nodeSet = hitTestResult.listBasedTestResult();
    for (auto& node : nodeSet) {
        if (RefPtr editableElement = rootEditableElement(node)) {
            ASSERT(searchRectInRootViewCoordinates.inclusivelyIntersects(editableElement->boundingBoxInRootViewCoordinates()));
            rootEditableElements.add(editableElement.releaseNonNull());
        }
    }

    // Fix up for a now empty focused inline element, e.g. <span contenteditable='true'>Hello</span> became
    // <span contenteditable='true'></span>. Hit testing will likely not find this element because the engine
    // tries to avoid creating line boxes, which are things it hit tests, for them to reduce memory. If the
    // focused element is inside the search rect it's the most likely target for future editing operations,
    // even if it's empty. So, we special case it here.
    RefPtr focusedOrMainFrame = focusController().focusedOrMainFrame();
    if (RefPtr focusedElement = focusedOrMainFrame ? focusedOrMainFrame->document()->focusedElement() : nullptr) {
        if (searchRectInRootViewCoordinates.inclusivelyIntersects(focusedElement->boundingBoxInRootViewCoordinates())) {
            if (RefPtr editableElement = rootEditableElement(*focusedElement))
                rootEditableElements.add(editableElement.releaseNonNull());
        }
    }
    return WTF::map(rootEditableElements, [](const auto& element) { return element.copyRef(); });
}

#if ENABLE(INTERACTION_REGIONS_IN_EVENT_REGION)
bool Page::shouldBuildInteractionRegions() const
{
    return m_settings->interactionRegionsEnabled();
}

void Page::setInteractionRegionsEnabled(bool enable)
{
    bool needsUpdate = enable && !shouldBuildInteractionRegions();
    m_settings->setInteractionRegionsEnabled(enable);
    if (needsUpdate) {
        if (RefPtr localMainFrame = this->localMainFrame())
            localMainFrame->invalidateContentEventRegionsIfNeeded(LocalFrame::InvalidateContentEventRegionsReason::Layout);
    }
}
#endif // ENABLE(INTERACTION_REGIONS_IN_EVENT_REGION)

const VisibleSelection& Page::selection() const
{
    RefPtr focusedOrMainFrame = focusController().focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return VisibleSelection::emptySelection();
    return focusedOrMainFrame->selection().selection();
}

void Page::setDefersLoading(bool defers)
{
    if (!m_settings->loadDeferringEnabled())
        return;

    if (m_settings->wantsBalancedSetDefersLoadingBehavior()) {
        ASSERT(defers || m_defersLoadingCallCount);
        if (defers && ++m_defersLoadingCallCount > 1)
            return;
        if (!defers && --m_defersLoadingCallCount)
            return;
    } else {
        ASSERT(!m_defersLoadingCallCount);
        if (defers == m_defersLoading)
            return;
    }

    m_defersLoading = defers;
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame))
            localFrame->loader().setDefersLoading(defers);
    }
}

void Page::clearUndoRedoOperations()
{
    m_editorClient->clearUndoRedoOperations();
}

bool Page::inLowQualityImageInterpolationMode() const
{
    return m_inLowQualityInterpolationMode;
}

void Page::setInLowQualityImageInterpolationMode(bool mode)
{
    m_inLowQualityInterpolationMode = mode;
}

DiagnosticLoggingClient& Page::diagnosticLoggingClient() const
{
    if (!settings().diagnosticLoggingEnabled() || !m_diagnosticLoggingClient)
        return emptyDiagnosticLoggingClient();
    return *m_diagnosticLoggingClient;
}

CheckedRef<DiagnosticLoggingClient> Page::checkedDiagnosticLoggingClient() const
{
    return diagnosticLoggingClient();
}

void Page::logMediaDiagnosticMessage(const RefPtr<FormData>& formData) const
{
    unsigned imageOrMediaFilesCount = formData ? formData->imageOrMediaFilesCount() : 0;
    if (!imageOrMediaFilesCount)
        return;
    auto message = makeString(imageOrMediaFilesCount, imageOrMediaFilesCount == 1 ? " media file has been submitted"_s : " media files have been submitted"_s);
    diagnosticLoggingClient().logDiagnosticMessageWithDomain(message, DiagnosticLoggingDomain::Media);
}

void Page::setMediaVolume(float volume)
{
    if (!(volume >= 0 && volume <= 1))
        return;

    if (m_mediaVolume == volume)
        return;

    m_mediaVolume = volume;

#if ENABLE(VIDEO)
    forEachMediaElement([] (HTMLMediaElement& element) {
        element.mediaVolumeDidChange();
    });
#endif
}

void Page::setZoomedOutPageScaleFactor(float scale)
{
    if (m_zoomedOutPageScaleFactor == scale)
        return;
    m_zoomedOutPageScaleFactor = scale;
    if (RefPtr localMainFrame = this->localMainFrame())
        localMainFrame->deviceOrPageScaleFactorChanged();
}

void Page::setPageScaleFactor(float scale, const IntPoint& origin, bool inStableState)
{
    LOG_WITH_STREAM(Viewports, stream << "Page " << this << " setPageScaleFactor " << scale << " at " << origin << " - stable " << inStableState);
    RefPtr mainDocument = localTopDocument();
    RefPtr mainFrameView = mainDocument ? mainDocument->view() : nullptr;

    if (scale == m_pageScaleFactor) {
        if (mainFrameView && mainFrameView->scrollPosition() != origin && !delegatesScaling())
            mainDocument->updateLayoutIgnorePendingStylesheets({ WebCore::LayoutOptions::UpdateCompositingLayers });
    } else {
        m_pageScaleFactor = scale;

        for (auto& rootFrame : m_rootFrames) {
            ASSERT(rootFrame->isRootFrame());
            RefPtr view = rootFrame->view();
            if (!view)
                continue;

            if (!delegatesScaling()) {
                view->setNeedsLayoutAfterViewConfigurationChange();
                view->setNeedsCompositingGeometryUpdate();
                view->setDescendantsNeedUpdateBackingAndHierarchyTraversal();

                if (RefPtr doc = rootFrame->document())
                    doc->resolveStyle(Document::ResolveStyleType::Rebuild);

                // Transform change on RenderView doesn't trigger repaint on non-composited contents.
                view->invalidateRect(IntRect(LayoutRect::infiniteRect()));
            }

            rootFrame->deviceOrPageScaleFactorChanged();

            if (view->fixedElementsLayoutRelativeToFrame())
                view->setViewportConstrainedObjectsNeedLayout();
        }

        if (mainFrameView && mainFrameView->scrollPosition() != origin && !delegatesScaling() && mainDocument->renderView() && mainDocument->renderView()->needsLayout() && mainFrameView->didFirstLayout()) {
            mainFrameView->layoutContext().layout();
            mainFrameView->layoutContext().updateCompositingLayersAfterLayoutIfNeeded();
        }
    }

    if (mainFrameView && mainFrameView->scrollPosition() != origin) {
        if (mainFrameView->delegatedScrollingMode() != DelegatedScrollingMode::DelegatedToNativeScrollView)
            mainFrameView->setScrollPosition(origin);
    }

#if ENABLE(VIDEO)
    if (inStableState) {
        forEachMediaElement([] (HTMLMediaElement& element) {
            element.pageScaleFactorChanged();
        });
    }
#else
    UNUSED_PARAM(inStableState);
#endif
}

void Page::setDelegatesScaling(bool delegatesScaling)
{
    m_delegatesScaling = delegatesScaling;
}

void Page::setViewScaleFactor(float scale)
{
    if (m_viewScaleFactor == scale)
        return;

    m_viewScaleFactor = scale;
    BackForwardCache::singleton().markPagesForDeviceOrPageScaleChanged(*this);
}

void Page::setDeviceScaleFactor(float scaleFactor)
{
    ASSERT(scaleFactor > 0);
    if (scaleFactor <= 0)
        return;
    
    if (m_deviceScaleFactor == scaleFactor)
        return;

    m_deviceScaleFactor = scaleFactor;
    setNeedsRecalcStyleInAllFrames();
    if (RefPtr localMainFrame = this->localMainFrame())
        localMainFrame->deviceOrPageScaleFactorChanged();
    BackForwardCache::singleton().markPagesForDeviceOrPageScaleChanged(*this);

    pageOverlayController().didChangeDeviceScaleFactor();
}

void Page::screenPropertiesDidChange()
{
#if ENABLE(VIDEO)
    auto mode = preferredDynamicRangeMode(protectedMainFrame()->protectedVirtualView().get());
    forEachMediaElement([mode] (auto& element) {
        element.setPreferredDynamicRangeMode(mode);
    });
#endif
#if HAVE(SUPPORT_HDR_DISPLAY)
    updateDisplayEDRHeadroom();
#endif

    updateScreenSupportedContentsFormats();

    setNeedsRecalcStyleInAllFrames();

    forEachRenderableDocument([this] (Document& document) {
        document.screenPropertiesDidChange(m_displayID);
    });
}

void Page::updateScreenSupportedContentsFormats()
{
#if HAVE(SUPPORT_HDR_DISPLAY)
    bool supportsHighDynamicRange = screenSupportsHighDynamicRange(m_displayID);
    if (m_screenSupportsHDR == supportsHighDynamicRange)
        return;
    m_screenSupportsHDR = supportsHighDynamicRange;

    forEachDocument([&] (Document& document) {
        if (!document.hasHDRContent())
            return;

        if (RefPtr view = document.view())
            view->setDescendantsNeedUpdateBackingAndHierarchyTraversal();
    });
#endif
}

void Page::windowScreenDidChange(PlatformDisplayID displayID, std::optional<FramesPerSecond> nominalFramesPerSecond)
{
    if (displayID == m_displayID && nominalFramesPerSecond == m_displayNominalFramesPerSecond)
        return;

    m_displayID = displayID;
    m_displayNominalFramesPerSecond = nominalFramesPerSecond;

    forEachDocument([&] (Document& document) {
        document.windowScreenDidChange(displayID);
    });

    updateScreenSupportedContentsFormats();

#if ENABLE(VIDEO)
    auto mode = preferredDynamicRangeMode(protectedMainFrame()->protectedVirtualView().get());
    forEachMediaElement([mode] (auto& element) {
        element.setPreferredDynamicRangeMode(mode);
    });
#endif

    if (RefPtr scrollingCoordinator = m_scrollingCoordinator)
        scrollingCoordinator->windowScreenDidChange(displayID, m_displayNominalFramesPerSecond);

    if (CheckedPtr scheduler = existingRenderingUpdateScheduler())
        scheduler->windowScreenDidChange(displayID);
    chrome().client().renderingUpdateFramesPerSecondChanged();

    setNeedsRecalcStyleInAllFrames();
}

void Page::setInitialScaleIgnoringContentSize(float scale)
{
    m_initialScaleIgnoringContentSize = scale;
}

void Page::setUserInterfaceLayoutDirection(UserInterfaceLayoutDirection userInterfaceLayoutDirection)
{
    if (m_userInterfaceLayoutDirection == userInterfaceLayoutDirection)
        return;

    m_userInterfaceLayoutDirection = userInterfaceLayoutDirection;
#if ENABLE(VIDEO)
    forEachMediaElement([] (HTMLMediaElement& element) {
        element.userInterfaceLayoutDirectionChanged();
    });
#endif
}

#if ENABLE(VIDEO)

void Page::updateMediaElementRateChangeRestrictions()
{
    // FIXME: This used to call this on all media elements, seemingly by accident. But was there some advantage to that for elements in the back/forward cache?
    forEachMediaElement([] (HTMLMediaElement& element) {
        element.updateRateChangeRestrictions();
    });
}

#endif

void Page::didStartProvisionalLoad()
{
    if (RefPtr performanceMonitor = m_performanceMonitor.get())
        performanceMonitor->didStartProvisionalLoad();

    if (m_settings->resourceLoadSchedulingEnabled())
        setLoadSchedulingMode(LoadSchedulingMode::Prioritized);
}

void Page::didCommitLoad()
{
#if ENABLE(EDITABLE_REGION)
    m_isEditableRegionEnabled = false;
#endif

    m_hasEverSetVisibilityAdjustment = false;

    m_mainFrameURLFragment = { };

#if PLATFORM(VISION) && ENABLE(GAMEPAD)
    initializeGamepadAccessForPageLoad();
#endif

    resetSeenPlugins();
    resetSeenMediaEngines();

#if ENABLE(IMAGE_ANALYSIS)
    resetTextRecognitionResults();
    resetImageAnalysisQueue();
#endif

#if ENABLE(GEOLOCATION)
    if (auto* geolocationController = GeolocationController::from(this))
        geolocationController->didNavigatePage();
#endif

    m_fixedContainerEdgesAndElements = std::make_pair(makeUniqueRef<FixedContainerEdges>(), WeakElementEdges { });

    m_elementTargetingController->reset();

    m_scriptTrackingPrivacyReports.clear();

    m_isWaitingForLoadToFinish = true;
}

void Page::didFinishLoad()
{
    resetRelevantPaintedObjectCounter();

    if (RefPtr performanceMonitor = m_performanceMonitor.get())
        performanceMonitor->didFinishLoad();

    setLoadSchedulingMode(LoadSchedulingMode::Direct);

    m_isWaitingForLoadToFinish = false;
}

bool Page::isOnlyNonUtilityPage() const
{
    return !isUtilityPage() && gNonUtilityPageCount == 1;
}

void Page::setLowPowerModeEnabledOverrideForTesting(std::optional<bool> isEnabled)
{
    // Remove ThrottlingReason::LowPowerMode so handleLowPowerModeChange() can do its work.
    m_throttlingReasonsOverridenForTesting.remove(ThrottlingReason::LowPowerMode);

    // Use the current low power mode value of the device.
    if (!isEnabled) {
        handleLowPowerModeChange(m_lowPowerModeNotifier->isLowPowerModeEnabled());
        return;
    }

    // Override the value and add ThrottlingReason::LowPowerMode so it override the device state.
    handleLowPowerModeChange(isEnabled.value());
    m_throttlingReasonsOverridenForTesting.add(ThrottlingReason::LowPowerMode);
}

void Page::setAggressiveThermalMitigationEnabledForTesting(std::optional<bool> isEnabled)
{
    m_throttlingReasonsOverridenForTesting.remove(ThrottlingReason::AggressiveThermalMitigation);

    if (!isEnabled.has_value()) {
        handleThermalMitigationChange(m_thermalMitigationNotifier->thermalMitigationEnabled());
        return;
    }

    handleThermalMitigationChange(isEnabled.value());
    m_throttlingReasonsOverridenForTesting.add(ThrottlingReason::AggressiveThermalMitigation);
}

void Page::setOutsideViewportThrottlingEnabledForTesting(bool isEnabled)
{
    if (!isEnabled)
        m_throttlingReasonsOverridenForTesting.add(ThrottlingReason::OutsideViewport);
    else
        m_throttlingReasonsOverridenForTesting.remove(ThrottlingReason::OutsideViewport);

    m_throttlingReasons.remove(ThrottlingReason::OutsideViewport);
}

void Page::setObscuredContentInsets(const FloatBoxExtent& obscuredContentInsets)
{
    if (m_obscuredContentInsets == obscuredContentInsets)
        return;

    m_obscuredContentInsets = obscuredContentInsets;
    RefPtr localMainFrame = this->localMainFrame();
    if (RefPtr view = localMainFrame ? localMainFrame->view() : nullptr)
        view->obscuredContentInsetsDidChange(obscuredContentInsets);
}

void Page::setShouldSuppressScrollbarAnimations(bool suppressAnimations)
{
    if (suppressAnimations == m_suppressScrollbarAnimations)
        return;

    lockAllOverlayScrollbarsToHidden(suppressAnimations);
    m_suppressScrollbarAnimations = suppressAnimations;
}

void Page::lockAllOverlayScrollbarsToHidden(bool lockOverlayScrollbars)
{
    RefPtr view = protectedMainFrame()->virtualView();
    if (!view)
        return;

    view->lockOverlayScrollbarStateToHidden(lockOverlayScrollbars);
    
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        RefPtr frameView = localFrame->view();
        if (!frameView)
            continue;

        auto scrollableAreas = frameView->scrollableAreas();
        if (!scrollableAreas)
            continue;

        for (CheckedRef area : *scrollableAreas)
            area->lockOverlayScrollbarStateToHidden(lockOverlayScrollbars);
    }
}

PageGroup& Page::group()
{
    if (!m_group)
        initGroup();
    return *m_group;
}
    
void Page::setVerticalScrollElasticity(ScrollElasticity elasticity)
{
    if (m_verticalScrollElasticity == elasticity)
        return;
    
    m_verticalScrollElasticity = elasticity;

    if (RefPtr view = protectedMainFrame()->virtualView())
        view->setVerticalScrollElasticity(elasticity);
}
    
void Page::setHorizontalScrollElasticity(ScrollElasticity elasticity)
{
    if (m_horizontalScrollElasticity == elasticity)
        return;
    
    m_horizontalScrollElasticity = elasticity;
    
    RefPtr localMainFrame = this->localMainFrame();
    if (RefPtr view = localMainFrame ? localMainFrame->view() : nullptr)
        view->setHorizontalScrollElasticity(elasticity);
}

void Page::setPagination(const Pagination& pagination)
{
    if (m_pagination == pagination)
        return;

    m_pagination = pagination;

    setNeedsRecalcStyleInAllFrames();
}

unsigned Page::pageCount() const
{
    if (m_pagination.mode == Pagination::Mode::Unpaginated)
        return 0;

    if (RefPtr localTopDocument = this->localTopDocument())
        localTopDocument->updateLayoutIgnorePendingStylesheets();

    return pageCountAssumingLayoutIsUpToDate();
}

unsigned Page::pageCountAssumingLayoutIsUpToDate() const
{
    if (m_pagination.mode == Pagination::Mode::Unpaginated)
        return 0;

    RefPtr localMainFrame = this->localMainFrame();
    ASSERT(!localMainFrame || !localMainFrame->view() || !localMainFrame->view()->needsLayout());
    CheckedPtr contentRenderer = localMainFrame ? localMainFrame->contentRenderer() : nullptr;
    return contentRenderer ? contentRenderer->pageCount() : 0;
}

void Page::setIsInWindow(bool isInWindow)
{
    setActivityState(isInWindow ? m_activityState | ActivityState::IsInWindow : m_activityState - ActivityState::IsInWindow);
}

void Page::setIsInWindowInternal(bool isInWindow)
{
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        if (RefPtr frameView = localFrame->view())
            frameView->setIsInWindow(isInWindow);
    }

    if (isInWindow)
        resumeAnimatingImages();
}

void Page::addActivityStateChangeObserver(ActivityStateChangeObserver& observer)
{
    m_activityStateChangeObservers.add(observer);
}

void Page::removeActivityStateChangeObserver(ActivityStateChangeObserver& observer)
{
    m_activityStateChangeObservers.remove(observer);
}

void Page::layoutIfNeeded(OptionSet<LayoutOptions> layoutOptions)
{
    for (auto& rootFrame : m_rootFrames) {
        ASSERT(rootFrame->isRootFrame());
        RefPtr view = rootFrame->view();
        if (!view)
            continue;

        view->updateLayoutAndStyleIfNeededRecursive(layoutOptions);
    }
}

void Page::scheduleRenderingUpdate(OptionSet<RenderingUpdateStep> requestedSteps)
{
    LOG_WITH_STREAM(EventLoop, stream << "Page " << this << " scheduleTimedRenderingUpdate() - requestedSteps " << requestedSteps << " remaining steps " << m_renderingUpdateRemainingSteps);
    if (m_renderingUpdateRemainingSteps.isEmpty()) {
        scheduleRenderingUpdateInternal();
        return;
    }
    computeUnfulfilledRenderingSteps(requestedSteps);
}

void Page::scheduleRenderingUpdateInternal()
{
    if (!chrome().client().scheduleRenderingUpdate())
        renderingUpdateScheduler().scheduleRenderingUpdate();
    m_renderingUpdateIsScheduled = true;
}

std::optional<MonotonicTime> Page::nextRenderingUpdateTimestamp() const
{
    if (!m_lastRenderingUpdateTimestamp)
        return std::nullopt;
    if (!m_renderingUpdateIsScheduled)
        return std::nullopt;
    auto interval = preferredRenderingUpdateInterval();
    auto now = MonotonicTime::now();
    ASSERT(now > m_lastRenderingUpdateTimestamp);
    return m_lastRenderingUpdateTimestamp + std::floor((now + interval - m_lastRenderingUpdateTimestamp) / interval) * interval;
}

void Page::didScheduleRenderingUpdate()
{
#if ENABLE(ASYNC_SCROLLING)
    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        scrollingCoordinator->didScheduleRenderingUpdate();
#endif
}

void Page::computeUnfulfilledRenderingSteps(OptionSet<RenderingUpdateStep> requestedSteps)
{
    // m_renderingUpdateRemainingSteps only has more than one entry for the re-entrant rendering update triggered by testing.
    // For scheduling, we only care about the value of the first entry.
    auto remainingSteps = m_renderingUpdateRemainingSteps[0];
    auto stepsForNextUpdate = requestedSteps - remainingSteps;
    m_unfulfilledRequestedSteps.add(stepsForNextUpdate);
}

void Page::triggerRenderingUpdateForTesting()
{
    LOG_WITH_STREAM(EventLoop, stream << "Page " << this << " triggerRenderingUpdateForTesting()");
    chrome().client().triggerRenderingUpdate();
}

void Page::startTrackingRenderingUpdates()
{
    m_isTrackingRenderingUpdates = true;
    m_renderingUpdateCount = 0;
}

unsigned Page::renderingUpdateCount() const
{
    return m_renderingUpdateCount;
}

// https://html.spec.whatwg.org/multipage/webappapis.html#update-the-rendering
void Page::updateRendering()
{
    LOG(EventLoop, "Page %p updateRendering() - re-entering %d", this, !m_renderingUpdateRemainingSteps.isEmpty());

    if (m_renderingUpdateRemainingSteps.isEmpty())
        m_unfulfilledRequestedSteps = { };

    m_renderingUpdateRemainingSteps.append(allRenderingUpdateSteps);

    // This function is not reentrant, e.g. a rAF callback may trigger a forces repaint in testing.
    // This is why we track m_renderingUpdateRemainingSteps as a stack.
    if (m_renderingUpdateRemainingSteps.size() > 1) {
        layoutIfNeeded(LayoutOptions::UpdateCompositingLayers);
        m_renderingUpdateRemainingSteps.last().remove(updateRenderingSteps);
        return;
    }

    m_lastRenderingUpdateTimestamp = MonotonicTime::now();
    m_renderingUpdateIsScheduled = false;

    bool isSVGImagePage = chrome().client().isSVGImageChromeClient();
    if (!isSVGImagePage)
        tracePoint(RenderingUpdateStart);

    layoutIfNeeded();

    auto runProcessingStep = [&](RenderingUpdateStep step, NOESCAPE const Function<void(Document&)>& perDocumentFunction) {
        m_renderingUpdateRemainingSteps.last().remove(step);
        forEachRenderableDocument(perDocumentFunction);
    };

    runProcessingStep(RenderingUpdateStep::RestoreScrollPositionAndViewState, [] (Document& document) {
        if (RefPtr frame = document.frame())
            frame->loader().restoreScrollPositionAndViewStateNowIfNeeded();
    });

#if ENABLE(ASYNC_SCROLLING)
    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        scrollingCoordinator->willStartRenderingUpdate();
#endif

    // Timestamps should not change while serving the rendering update steps.
    Vector<WeakPtr<Document, WeakPtrImplWithEventTargetData>> initialDocuments;
    forEachDocument([&initialDocuments] (Document& document) {
        document.protectedWindow()->freezeNowTimestamp();
        initialDocuments.append(document);
    });

    runProcessingStep(RenderingUpdateStep::Reveal, [] (Document& document) {
        // FIXME: Bug 278193 - Hidden docs should already be excluded.
        if (document.visibilityState() != VisibilityState::Hidden)
            document.reveal();
    });

    runProcessingStep(RenderingUpdateStep::FlushAutofocusCandidates, [] (Document& document) {
        if (document.isTopDocument())
            document.flushAutofocusCandidates();
    });

    runProcessingStep(RenderingUpdateStep::Resize, [] (Document& document) {
        document.runResizeSteps();
    });

    runProcessingStep(RenderingUpdateStep::Scroll, [] (Document& document) {
        document.runScrollSteps();
    });

    runProcessingStep(RenderingUpdateStep::MediaQueryEvaluation, [] (Document& document) {
        document.evaluateMediaQueriesAndReportChanges();
    });

    // FIXME: This suppression shouldn't be needed.
    SUPPRESS_UNCOUNTED_LAMBDA_CAPTURE runProcessingStep(RenderingUpdateStep::AdjustVisibility, [&] (auto& document) {
        m_elementTargetingController->adjustVisibilityInRepeatedlyTargetedRegions(document);
    });

    runProcessingStep(RenderingUpdateStep::Animations, [] (Document& document) {
        document.updateAnimationsAndSendEvents();
    });

#if ENABLE(FULLSCREEN_API)
    runProcessingStep(RenderingUpdateStep::Fullscreen, [] (Document& document) {
        document.protectedFullscreen()->dispatchPendingEvents();
    });
#else
    m_renderingUpdateRemainingSteps.last().remove(RenderingUpdateStep::Fullscreen);
#endif

    runProcessingStep(RenderingUpdateStep::VideoFrameCallbacks, [] (Document& document) {
        document.serviceRequestVideoFrameCallbacks();
    });

    runProcessingStep(RenderingUpdateStep::AnimationFrameCallbacks, [] (Document& document) {
        document.serviceRequestAnimationFrameCallbacks();
    });

    runProcessingStep(RenderingUpdateStep::CaretAnimation, [] (Document& document) {
        document.serviceCaretAnimation();
    });

    layoutIfNeeded();

    // FIXME: This suppression shouldn't be needed.
    SUPPRESS_UNCOUNTED_LAMBDA_CAPTURE runProcessingStep(RenderingUpdateStep::ResizeObservations, [&] (Document& document) {
        document.updateResizeObservations(*this);
    });

    // https://drafts.csswg.org/scroll-animations-1/#event-loop
    forEachDocument([] (Document& document) {
        document.updateStaleScrollTimelines();
    });

    runProcessingStep(RenderingUpdateStep::FocusFixup, [&] (Document& document) {
        if (RefPtr focusedElement = document.focusedElement()) {
            if (!focusedElement->isFocusable())
                document.setFocusedElement(nullptr);
        }
    });

    runProcessingStep(RenderingUpdateStep::UpdateContentRelevancy, [] (Document& document) {
        document.updateRelevancyOfContentVisibilityElements();
    });

    runProcessingStep(RenderingUpdateStep::PerformPendingViewTransitions, [] (Document& document) {
        document.performPendingViewTransitions();
    });

    runProcessingStep(RenderingUpdateStep::IntersectionObservations, [] (Document& document) {
        document.updateIntersectionObservations();
    });

    runProcessingStep(RenderingUpdateStep::Images, [] (Document& document) {
        for (auto& image : document.protectedCachedResourceLoader()->allCachedSVGImages()) {
            if (RefPtr page = image->internalPage())
                page->isolatedUpdateRendering();
        }
    });

    runProcessingStep(RenderingUpdateStep::UpdateValidationMessagePositions, [] (Document& document) {
        document.adjustValidationMessagePositions();
    });

    runProcessingStep(RenderingUpdateStep::SnapshottedScrollOffsets, [&] (Document& document) {
        Style::AnchorPositionEvaluator::updateSnapshottedScrollOffsets(document);
    });

    for (auto& document : initialDocuments) {
        if (document && document->window())
            document->protectedWindow()->unfreezeNowTimestamp();
    }

    m_renderingUpdateRemainingSteps.last().remove(RenderingUpdateStep::WheelEventMonitorCallbacks);

    if (isMonitoringWheelEvents()) [[unlikely]]
        wheelEventTestMonitor()->checkShouldFireCallbacks();

    if (m_isTrackingRenderingUpdates)
        ++m_renderingUpdateCount;

    layoutIfNeeded(LayoutOptions::UpdateCompositingLayers);
    doAfterUpdateRendering();

    if (!isSVGImagePage)
        tracePoint(RenderingUpdateEnd);
}

void Page::isolatedUpdateRendering()
{
    LOG(EventLoop, "Page %p isolatedUpdateRendering()", this);
    updateRendering();
    renderingUpdateCompleted();
}

void Page::doAfterUpdateRendering()
{
    // Code here should do once-per-frame work that needs to be done before painting, and requires
    // layout to be up-to-date. It should not run script, trigger layout, or dirty layout.

    auto runProcessingStep = [&](RenderingUpdateStep step, NOESCAPE const Function<void(Document&)>& perDocumentFunction) {
        m_renderingUpdateRemainingSteps.last().remove(step);
        forEachRenderableDocument(perDocumentFunction);
    };

    runProcessingStep(RenderingUpdateStep::CursorUpdate, [] (Document& document) {
        if (RefPtr frame = document.frame())
            frame->eventHandler().updateCursorIfNeeded();
    });

    forEachRenderableDocument([] (Document& document) {
        document.enqueuePaintTimingEntryIfNeeded();
    });

    forEachRenderableDocument([] (Document& document) {
        document.enqueueEventTimingEntriesIfNeeded();
    });

    forEachRenderableDocument([] (Document& document) {
        document.selection().updateAppearanceAfterUpdatingRendering();
    });

    forEachRenderableDocument([] (Document& document) {
        document.updateHighlightPositions();
    });

#if ENABLE(APP_HIGHLIGHTS)
    forEachRenderableDocument([timestamp = m_lastRenderingUpdateTimestamp] (Document& document) {
        document.restoreUnrestoredAppHighlights(timestamp);
    });
#endif

#if ENABLE(VIDEO)
    forEachRenderableDocument([] (Document& document) {
        document.updateTextTrackRepresentationImageIfNeeded();
    });
#endif

#if ENABLE(IMAGE_ANALYSIS)
    updateElementsWithTextRecognitionResults();
#endif

    updateValidationMessages();

    prioritizeVisibleResources();

    m_renderingUpdateRemainingSteps.last().remove(RenderingUpdateStep::EventRegionUpdate);

    RefPtr localMainFrame = this->localMainFrame();
#if ENABLE(IOS_TOUCH_EVENTS)
    // updateTouchEventRegions() needs to be called only on the top document.
    if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr)
        document->updateTouchEventRegions();
#endif
    forEachDocument([] (Document& document) {
        document.updateEventRegions();
    });

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
    m_renderingUpdateRemainingSteps.last().remove(RenderingUpdateStep::AccessibilityRegionUpdate);
    if (shouldUpdateAccessibilityRegions()) {
        m_lastAccessibilityObjectRegionsUpdate = m_lastRenderingUpdateTimestamp;

        if (m_axObjectCache)
            m_axObjectCache->onAccessibilityPaintStarted();

        forEachRenderableDocument([] (Document& document) {
            document.updateAccessibilityObjectRegions();
        });

        if (m_axObjectCache)
            m_axObjectCache->onAccessibilityPaintFinished();
    }
#endif

    DebugPageOverlays::doAfterUpdateRendering(*this);

    m_renderingUpdateRemainingSteps.last().remove(RenderingUpdateStep::PrepareCanvasesForDisplayOrFlush);

    forEachRenderableDocument([] (Document& document) {
        document.prepareCanvasesForDisplayOrFlushIfNeeded();
    });

    if (localMainFrame) {
        ASSERT(!localMainFrame->view() || !localMainFrame->view()->needsLayout());
#if ASSERT_ENABLED
        for (auto* child = localMainFrame->tree().firstRenderedChild(); child; child = child->tree().traverseNextRendered()) {
            auto* localFrame = dynamicDowncast<LocalFrame>(child);
            auto* frameView = localFrame->view();
            ASSERT(!frameView || !frameView->needsLayout());
        }
#endif

        if (RefPtr view = localMainFrame->view())
            view->notifyAllFramesThatContentAreaWillPaint();
    }

    computeSampledPageTopColorIfNecessary();
}

void Page::finalizeRenderingUpdate(OptionSet<FinalizeRenderingUpdateFlags> flags)
{
    for (auto& rootFrame : m_rootFrames)
        finalizeRenderingUpdateForRootFrame(Ref { rootFrame.get() }, flags);

    ASSERT(m_renderingUpdateRemainingSteps.last().isEmpty());
    renderingUpdateCompleted();
}

void Page::finalizeRenderingUpdateForRootFrame(LocalFrame& rootFrame, OptionSet<FinalizeRenderingUpdateFlags> flags)
{
    LOG(EventLoop, "Page %p finalizeRenderingUpdate()", this);

    ASSERT(rootFrame.isRootFrame());
    RefPtr view = rootFrame.view();
    if (!view)
        return;

    if (flags.contains(FinalizeRenderingUpdateFlags::InvalidateImagesWithAsyncDecodes))
        view->invalidateImagesWithAsyncDecodes();

    m_renderingUpdateRemainingSteps.last().remove(RenderingUpdateStep::LayerFlush);

    view->flushCompositingStateIncludingSubframes();

#if ENABLE(ASYNC_SCROLLING)
    m_renderingUpdateRemainingSteps.last().remove(RenderingUpdateStep::ScrollingTreeUpdate);

    if (RefPtr scrollingCoordinator = this->scrollingCoordinator()) {
        scrollingCoordinator->commitTreeStateIfNeeded();
        if (flags.contains(FinalizeRenderingUpdateFlags::ApplyScrollingTreeLayerPositions))
            scrollingCoordinator->applyScrollingTreeLayerPositions();

        scrollingCoordinator->didCompleteRenderingUpdate();
    }
#endif
}

void Page::renderingUpdateCompleted()
{
    m_renderingUpdateRemainingSteps.removeLast();

    LOG_WITH_STREAM(EventLoop, stream << "Page " << this << " renderingUpdateCompleted() - steps " << m_renderingUpdateRemainingSteps << " unfulfilled steps " << m_unfulfilledRequestedSteps);

    if (m_unfulfilledRequestedSteps) {
        scheduleRenderingUpdateInternal();
        m_unfulfilledRequestedSteps = { };
    }

    if (!isUtilityPage()) {
        auto nextRenderingUpdate = m_lastRenderingUpdateTimestamp + preferredRenderingUpdateInterval();
        protectedOpportunisticTaskScheduler()->rescheduleIfNeeded(nextRenderingUpdate);
    }
}

Ref<OpportunisticTaskScheduler> Page::protectedOpportunisticTaskScheduler() const
{
    return m_opportunisticTaskScheduler;
}

void Page::willStartRenderingUpdateDisplay()
{
    LOG_WITH_STREAM(EventLoop, stream << "Page " << this << " willStartRenderingUpdateDisplay()");

    // Inspector's use of "composite" is rather innacurate. On Apple platforms, the "composite" step happens
    // in another process; these hooks wrap the non-WebKit CA commit time which is mostly painting-related.
    if (RefPtr localMainFrame = this->localMainFrame())
        m_inspectorController->willComposite(*localMainFrame);

    if (RefPtr scrollingCoordinator = m_scrollingCoordinator)
        scrollingCoordinator->willStartPlatformRenderingUpdate();
}

void Page::didCompleteRenderingUpdateDisplay()
{
    LOG_WITH_STREAM(EventLoop, stream << "Page " << this << " didCompleteRenderingUpdateDisplay()");

    if (RefPtr scrollingCoordinator = m_scrollingCoordinator)
        scrollingCoordinator->didCompletePlatformRenderingUpdate();

    if (RefPtr localMainFrame = this->localMainFrame())
        m_inspectorController->didComposite(*localMainFrame);
}

void Page::didCompleteRenderingFrame()
{
    LOG_WITH_STREAM(EventLoop, stream << "Page " << this << " didCompleteRenderingFrame()");

    // FIXME: This is where we'd call requestPostAnimationFrame callbacks: webkit.org/b/249798.
    // FIXME: Run WindowEventLoop tasks from here: webkit.org/b/249684.
    InspectorInstrumentation::didCompleteRenderingFrame(m_mainFrame);
}

void Page::didUpdateRendering()
{
    LOG_WITH_STREAM(EventLoop, stream << "Page " << this << " didUpdateRendering()");
    forEachDocument([&] (Document& document) {
        document.flushDeferredRenderingIsSuppressedForViewTransitionChanges();
    });
}

void Page::prioritizeVisibleResources()
{
    if (loadSchedulingMode() == LoadSchedulingMode::Direct)
        return;
    RefPtr localTopDocument = this->localTopDocument();
    if (!localTopDocument)
        return;

    Vector<CachedResourceHandle<CachedResource>> toPrioritize;

    forEachRenderableDocument([&] (Document& document) {
        toPrioritize.appendVector(document.protectedCachedResourceLoader()->visibleResourcesToPrioritize());
    });
    
    auto computeSchedulingMode = [&] {
        // Parsing generates resource loads.
        if (localTopDocument->parsing())
            return LoadSchedulingMode::Prioritized;
        
        // Async script execution may generate more resource loads that benefit from prioritization.
        if (CheckedPtr scriptRunner = localTopDocument->scriptRunnerIfExists(); scriptRunner && scriptRunner->hasPendingScripts())
            return LoadSchedulingMode::Prioritized;
        
        // We still haven't finished loading the visible resources.
        if (!toPrioritize.isEmpty())
            return LoadSchedulingMode::Prioritized;
        
        return LoadSchedulingMode::Direct;
    };
    
    setLoadSchedulingMode(computeSchedulingMode());

    if (toPrioritize.isEmpty())
        return;

    auto resourceLoaders = toPrioritize.map([](auto& resource) {
        return resource->loader();
    });

    platformStrategies()->loaderStrategy()->prioritizeResourceLoads(resourceLoaders);
}

void Page::setLoadSchedulingMode(LoadSchedulingMode mode)
{
    if (m_loadSchedulingMode == mode)
        return;

    m_loadSchedulingMode = mode;

    platformStrategies()->loaderStrategy()->setResourceLoadSchedulingMode(*this, m_loadSchedulingMode);
}

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
bool Page::shouldUpdateAccessibilityRegions() const
{
    static constexpr Seconds updateInterval { 750_ms };
    if (!AXObjectCache::accessibilityEnabled() || !AXObjectCache::isIsolatedTreeEnabled())
        return false;

    ASSERT(m_lastRenderingUpdateTimestamp >= m_lastAccessibilityObjectRegionsUpdate);
    if ((m_lastRenderingUpdateTimestamp - m_lastAccessibilityObjectRegionsUpdate) < updateInterval) {
        // We've already updated accessibility object rects recently, so skip this update and schedule another for later.

        RefPtr<Document> protectedMainDocument;
        if (RefPtr localMainFrame = this->localMainFrame())
            protectedMainDocument = localMainFrame ? localMainFrame->document() : nullptr;
        else if (RefPtr remoteFrame = dynamicDowncast<RemoteFrame>(mainFrame())) {
            if (RefPtr owner = remoteFrame->ownerElement())
                protectedMainDocument = owner->document();
        }

        // If accessibility is enabled and we have a main document, that document should have an AX object cache.
        ASSERT(!protectedMainDocument || protectedMainDocument->existingAXObjectCache());
        if (CheckedPtr topAxObjectCache = protectedMainDocument ? protectedMainDocument->existingAXObjectCache() : nullptr)
            topAxObjectCache->scheduleObjectRegionsUpdate();
        return false;
    }
    return true;
}
#endif

#if ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
void Page::setImageAnimationEnabled(bool enabled)
{
    if (!settings().imageAnimationControlEnabled())
        return;

    // This method overrides any individually set animation play-states (so we need to do work even if `enabled` is
    // already equal to `m_imageAnimationEnabled` because there may be individually playing or paused images).
    m_imageAnimationEnabled = enabled;
    updatePlayStateForAllAnimations();
    chrome().client().isAnyAnimationAllowedToPlayDidChange(enabled);
}
#endif // ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)

#if ENABLE(ACCESSIBILITY_NON_BLINKING_CURSOR)
void Page::setPrefersNonBlinkingCursor(bool enabled)
{
    m_prefersNonBlinkingCursor = enabled;
}
#endif

void Page::suspendScriptedAnimations()
{
    m_scriptedAnimationsSuspended = true;

    forEachDocument([] (Document& document) {
        document.suspendScriptedAnimationControllerCallbacks();
    });
}

void Page::resumeScriptedAnimations()
{
    m_scriptedAnimationsSuspended = false;

    forEachDocument([] (Document& document) {
        document.resumeScriptedAnimationControllerCallbacks();
    });
}

void Page::timelineControllerMaximumAnimationFrameRateDidChange(AnimationTimelinesController&)
{
    if (CheckedPtr scheduler = existingRenderingUpdateScheduler())
        scheduler->adjustRenderingUpdateFrequency();
    chrome().client().renderingUpdateFramesPerSecondChanged();
}

std::optional<FramesPerSecond> Page::preferredRenderingUpdateFramesPerSecond(OptionSet<PreferredRenderingUpdateOption> flags) const
{
    // Unless the call site specifies an explicit set of options, this method will account for both
    // throttling reasons and the frame rate of animations to determine its return value. The only
    // place where we specify an explicit set of options is AnimationTimelinesController::updateAnimationsAndSendEvents()
    // where we need to identify what the update frame rate would be _not_ accounting for animations.

    auto throttlingReasons = m_throttlingReasons;
    if (!flags.contains(PreferredRenderingUpdateOption::IncludeThrottlingReasons))
        throttlingReasons = { };

    auto frameRate = preferredFramesPerSecond(throttlingReasons, m_displayNominalFramesPerSecond, settings().preferPageRenderingUpdatesNear60FPSEnabled());
    if (!flags.contains(PreferredRenderingUpdateOption::IncludeAnimationsFrameRate))
        return frameRate;

    // If we're throttled, we do not account for the frame rate set on animations and simply use the throttled frame rate.
    auto unthrottledDefaultFrameRate = preferredRenderingUpdateFramesPerSecond({ });
    auto isThrottled = frameRate && unthrottledDefaultFrameRate && *frameRate < *unthrottledDefaultFrameRate;
    if (isThrottled)
        return frameRate;

    forEachDocument([&] (Document& document) {
        if (CheckedPtr timelinesController = document.timelinesController()) {
            if (auto timelinePreferredFrameRate = timelinesController->maximumAnimationFrameRate()) {
                if (!frameRate || *frameRate < *timelinePreferredFrameRate)
                    frameRate = *timelinePreferredFrameRate;
            }
        }
    });

    return frameRate;
}

Seconds Page::preferredRenderingUpdateInterval() const
{
    return preferredFrameInterval(m_throttlingReasons, m_displayNominalFramesPerSecond, settings().preferPageRenderingUpdatesNear60FPSEnabled());
}

void Page::setIsVisuallyIdleInternal(bool isVisuallyIdle)
{
    if (isVisuallyIdle == m_throttlingReasons.contains(ThrottlingReason::VisuallyIdle))
        return;

    m_throttlingReasons.set(ThrottlingReason::VisuallyIdle, isVisuallyIdle);
    if (CheckedPtr scheduler = existingRenderingUpdateScheduler())
        scheduler->adjustRenderingUpdateFrequency();
    chrome().client().renderingUpdateFramesPerSecondChanged();
}

void Page::handleLowPowerModeChange(bool isLowPowerModeEnabled)
{
    if (!canUpdateThrottlingReason(ThrottlingReason::LowPowerMode))
        return;

    if (isLowPowerModeEnabled == m_throttlingReasons.contains(ThrottlingReason::LowPowerMode))
        return;

    m_throttlingReasons.set(ThrottlingReason::LowPowerMode, isLowPowerModeEnabled);
    if (CheckedPtr scheduler = existingRenderingUpdateScheduler())
        scheduler->adjustRenderingUpdateFrequency();
    chrome().client().renderingUpdateFramesPerSecondChanged();

    updateDOMTimerAlignmentInterval();
}

void Page::handleThermalMitigationChange(bool thermalMitigationEnabled)
{
    if (!canUpdateThrottlingReason(ThrottlingReason::ThermalMitigation))
        return;

    if (thermalMitigationEnabled == m_throttlingReasons.contains(ThrottlingReason::ThermalMitigation))
        return;

    m_throttlingReasons.set(ThrottlingReason::ThermalMitigation, thermalMitigationEnabled);

    if (settings().respondToThermalPressureAggressively()) {
        m_throttlingReasons.set(ThrottlingReason::AggressiveThermalMitigation, thermalMitigationEnabled);
        if (CheckedPtr scheduler = existingRenderingUpdateScheduler())
            scheduler->adjustRenderingUpdateFrequency();
        chrome().client().renderingUpdateFramesPerSecondChanged();
    }

    RELEASE_LOG(PerformanceLogging, "%p - Page::handleThermalMitigationChange: thermal mitigation %d, aggressive thermal mitigation %d", this, isThermalMitigationEnabled(), isAggressiveThermalMitigationEnabled());

    updateDOMTimerAlignmentInterval();
}

void Page::userStyleSheetLocationChanged()
{
    // FIXME: Eventually we will move to a model of just being handed the sheet
    // text instead of loading the URL ourselves.
    URL url = m_settings->userStyleSheetLocation();
    
    // Allow any local file URL scheme to be loaded.
    if (LegacySchemeRegistry::shouldTreatURLSchemeAsLocal(url.protocol()))
        m_userStyleSheetPath = url.fileSystemPath();
    else
        m_userStyleSheetPath = String();

    m_didLoadUserStyleSheet = false;
    m_userStyleSheet = String();
    m_userStyleSheetModificationTime = std::nullopt;

    // Data URLs with base64-encoded UTF-8 style sheets are common. We can process them
    // synchronously and avoid using a loader. 
    if (url.protocolIsData() && url.string().startsWith("data:text/css;charset=utf-8;base64,"_s)) {
        m_didLoadUserStyleSheet = true;

        String styleSheetAsBase64 = base64DecodeToString(PAL::decodeURLEscapeSequences(StringView(url.string()).substring(35)), { Base64DecodeOption::ValidatePadding, Base64DecodeOption::IgnoreWhitespace });
        if (!styleSheetAsBase64.isNull())
            m_userStyleSheet = styleSheetAsBase64;
    }

    forEachDocument([] (Document& document) {
        document.checkedExtensionStyleSheets()->updatePageUserSheet();
    });
}

const String& Page::userStyleSheet() const
{
    if (m_userStyleSheetPath.isEmpty())
        return m_userStyleSheet;

    auto modificationTime = FileSystem::fileModificationTime(m_userStyleSheetPath);
    if (!modificationTime) {
        // The stylesheet either doesn't exist, was just deleted, or is
        // otherwise unreadable. If we've read the stylesheet before, we should
        // throw away that data now as it no longer represents what's on disk.
        m_userStyleSheet = String();
        return m_userStyleSheet;
    }

    // If the stylesheet hasn't changed since the last time we read it, we can
    // just return the old data.
    if (m_didLoadUserStyleSheet && (m_userStyleSheetModificationTime && modificationTime.value() <= m_userStyleSheetModificationTime.value()))
        return m_userStyleSheet;

    m_didLoadUserStyleSheet = true;
    m_userStyleSheet = String();
    m_userStyleSheetModificationTime = modificationTime;

    // FIXME: It would be better to load this asynchronously to avoid blocking
    // the process, but we will first need to create an asynchronous loading
    // mechanism that is not tied to a particular Frame. We will also have to
    // determine what our behavior should be before the stylesheet is loaded
    // and what should happen when it finishes loading, especially with respect
    // to when the load event fires, when Document::close is called, and when
    // layout/paint are allowed to happen.
    RefPtr data = SharedBuffer::createWithContentsOfFile(m_userStyleSheetPath);
    if (!data)
        return m_userStyleSheet;

    m_userStyleSheet = TextResourceDecoder::create(cssContentTypeAtom())->decodeAndFlush(data->span());

    return m_userStyleSheet;
}

void Page::userAgentChanged()
{
    forEachDocument([] (Document& document) {
        if (RefPtr window = document.window()) {
            if (RefPtr navigator = window->optionalNavigator())
                navigator->userAgentChanged();
        }
    });
}

void Page::invalidateStylesForAllLinks()
{
    forEachDocument([] (Document& document) {
        if (CheckedPtr visitedLinkState = document.visitedLinkStateIfExists())
            visitedLinkState->invalidateStyleForAllLinks();
    });
}

void Page::invalidateStylesForLink(SharedStringHash linkHash)
{
    forEachDocument([&] (Document& document) {
        if (CheckedPtr visitedLinkState = document.visitedLinkStateIfExists())
            visitedLinkState->invalidateStyleForLink(linkHash);
    });
}

void Page::invalidateInjectedStyleSheetCacheInAllFrames()
{
    forEachDocument([] (Document& document) {
        if (CheckedPtr extensionStyleSheets = document.extensionStyleSheetsIfExists())
            extensionStyleSheets->invalidateInjectedStyleSheetCache();
    });
}

void Page::setDebugger(JSC::Debugger* debugger)
{
    if (m_debugger == debugger)
        return;

    m_debugger = debugger;

    for (RefPtr frame = m_mainFrame.get(); frame; frame = frame->tree().traverseNext())
        frame->protectedWindowProxy()->attachDebugger(m_debugger);
}

bool Page::hasCustomHTMLTokenizerTimeDelay() const
{
    return m_settings->maxParseDuration() != -1;
}

double Page::customHTMLTokenizerTimeDelay() const
{
    ASSERT(m_settings->maxParseDuration() != -1);
    return m_settings->maxParseDuration();
}

void Page::setCORSDisablingPatterns(Vector<UserContentURLPattern>&& patterns)
{
    m_corsDisablingPatterns = WTFMove(patterns);
}

void Page::addCORSDisablingPatternForTesting(UserContentURLPattern&& pattern)
{
    m_corsDisablingPatterns.append(WTFMove(pattern));
}

void Page::setMemoryCacheClientCallsEnabled(bool enabled)
{
    if (m_areMemoryCacheClientCallsEnabled == enabled)
        return;

    m_areMemoryCacheClientCallsEnabled = enabled;
    if (!enabled || !m_hasPendingMemoryCacheLoadNotifications)
        return;

    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame))
            localFrame->loader().tellClientAboutPastMemoryCacheLoads();
    }
    m_hasPendingMemoryCacheLoadNotifications = false;
}

void Page::setEditable(bool isEditable)
{
    m_isEditable = isEditable;

    if (CheckedPtr cache = axObjectCache()) {
        forEachDocument([&] (Document& document) {
            cache->handlePageEditibilityChanged(document);
        });
    }
}

void Page::hiddenPageDOMTimerThrottlingStateChanged()
{
    // Disable & reengage to ensure state is updated.
    setTimerThrottlingState(TimerThrottlingState::Disabled);
    updateTimerThrottlingState();
}

void Page::updateTimerThrottlingState()
{
    // Timer throttling disabled if page is visually active, or disabled by setting.
    if (!m_settings->hiddenPageDOMTimerThrottlingEnabled() || !(m_activityState & ActivityState::IsVisuallyIdle)) {
        setTimerThrottlingState(TimerThrottlingState::Disabled);
        return;
    }

    // If the page is visible (but idle), there is any activity (loading, media playing, etc), or per setting,
    // we allow timer throttling, but not increasing timer throttling.
    if (!m_settings->hiddenPageDOMTimerThrottlingAutoIncreases()
        || m_activityState.containsAny({ActivityState::IsVisible, ActivityState::IsAudible, ActivityState::IsLoading, ActivityState::IsCapturingMedia })) {
        setTimerThrottlingState(TimerThrottlingState::Enabled);
        return;
    }

    // If we get here increasing timer throttling is enabled.
    setTimerThrottlingState(TimerThrottlingState::EnabledIncreasing);
}

void Page::setTimerThrottlingState(TimerThrottlingState state)
{
    if (state == m_timerThrottlingState)
        return;

    m_timerThrottlingState = state;
    m_timerThrottlingStateLastChangedTime = MonotonicTime::now();

    updateDOMTimerAlignmentInterval();

    // When throttling is disabled, release all throttled timers.
    if (state == TimerThrottlingState::Disabled) {
        forEachDocument([] (Document& document) {
            document.didChangeTimerAlignmentInterval();
        });
    }
}

void Page::setDOMTimerAlignmentIntervalIncreaseLimit(Seconds limit)
{
    m_domTimerAlignmentIntervalIncreaseLimit = limit;

    // If (m_domTimerAlignmentIntervalIncreaseLimit < m_domTimerAlignmentInterval) then we need
    // to update m_domTimerAlignmentInterval, if greater then need to restart the increase timer.
    if (m_timerThrottlingState == TimerThrottlingState::EnabledIncreasing)
        updateDOMTimerAlignmentInterval();
}

void Page::updateDOMTimerAlignmentInterval()
{
    bool needsIncreaseTimer = false;

    switch (m_timerThrottlingState) {
    case TimerThrottlingState::Disabled: {
        bool isInLowPowerOrThermallyMitigatedMode = isLowPowerModeEnabled() || isThermalMitigationEnabled();
        m_domTimerAlignmentInterval = isInLowPowerOrThermallyMitigatedMode ? DOMTimer::defaultAlignmentIntervalInLowPowerOrThermallyMitigatedMode() : DOMTimer::defaultAlignmentInterval();
        break;
    }
    case TimerThrottlingState::Enabled:
        m_domTimerAlignmentInterval = DOMTimer::hiddenPageAlignmentInterval();
        break;

    case TimerThrottlingState::EnabledIncreasing:
        // For pages in prerender state maximum throttling kicks in immediately.
        if (m_isPrerender)
            m_domTimerAlignmentInterval = m_domTimerAlignmentIntervalIncreaseLimit;
        else {
            ASSERT(!!m_timerThrottlingStateLastChangedTime);
            m_domTimerAlignmentInterval = MonotonicTime::now() - m_timerThrottlingStateLastChangedTime;
            // If we're below the limit, set the timer. If above, clamp to limit.
            if (m_domTimerAlignmentInterval < m_domTimerAlignmentIntervalIncreaseLimit)
                needsIncreaseTimer = true;
            else
                m_domTimerAlignmentInterval = m_domTimerAlignmentIntervalIncreaseLimit;
        }
        // Alignment interval should not be less than DOMTimer::hiddenPageAlignmentInterval().
        m_domTimerAlignmentInterval = std::max(m_domTimerAlignmentInterval, DOMTimer::hiddenPageAlignmentInterval());
    }

    // If throttling is enabled, auto-increasing of throttling is enabled, and the auto-increase
    // limit has not yet been reached, and then arm the timer to consider an increase. Time to wait
    // between increases is equal to the current throttle time. Since alignment interval increases
    // exponentially, time between steps is exponential too.
    if (!needsIncreaseTimer)
        m_domTimerAlignmentIntervalIncreaseTimer.stop();
    else if (!m_domTimerAlignmentIntervalIncreaseTimer.isActive())
        m_domTimerAlignmentIntervalIncreaseTimer.startOneShot(m_domTimerAlignmentInterval);
}

void Page::domTimerAlignmentIntervalIncreaseTimerFired()
{
    ASSERT(m_settings->hiddenPageDOMTimerThrottlingAutoIncreases());
    ASSERT(m_timerThrottlingState == TimerThrottlingState::EnabledIncreasing);
    ASSERT(m_domTimerAlignmentInterval < m_domTimerAlignmentIntervalIncreaseLimit);

    // Alignment interval is increased to equal the time the page has been throttled, to a limit.
    updateDOMTimerAlignmentInterval();
}

void Page::storageBlockingStateChanged()
{
    forEachDocument([] (Document& document) {
        document.storageBlockingStateDidChange();
    });
}

void Page::updateIsPlayingMedia()
{
    MediaProducerMediaStateFlags state;
    forEachDocument([&](auto& document) {
        state.add(document.mediaState());
    });

    if (state == m_mediaState)
        return;

    m_mediaState = state;

    chrome().client().isPlayingMediaDidChange(state);
}

void Page::schedulePlaybackControlsManagerUpdate()
{
#if ENABLE(VIDEO)
    if (!m_playbackControlsManagerUpdateTimer.isActive())
        m_playbackControlsManagerUpdateTimer.startOneShot(0_s);
#endif
}

#if ENABLE(VIDEO)

RefPtr<HTMLMediaElement> Page::bestMediaElementForRemoteControls(MediaElementSession::PlaybackControlsPurpose purpose, Document* document)
{
    auto selectedSession = mediaSessionManager().bestEligibleSessionForRemoteControls([document] (auto& session) {
        auto* mediaElementSession = dynamicDowncast<MediaElementSession>(session);
        if (!mediaElementSession)
            return false;

        RefPtr element = mediaElementSession->element().get();
        if (!element)
            return false;

        return !document || &element->document() == document;
    }, purpose);

    if (RefPtr mediaElementSession = dynamicDowncast<MediaElementSession>(selectedSession.get()))
        return mediaElementSession->protectedElement();

    return nullptr;
}

void Page::playbackControlsManagerUpdateTimerFired()
{
    WeakPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    RefPtr document = localMainFrame ? localMainFrame->document() : nullptr;
    if (auto bestMediaElement = bestMediaElementForRemoteControls(MediaElementSession::PlaybackControlsPurpose::ControlsManager, document.get()))
        chrome().client().setUpPlaybackControlsManager(*bestMediaElement);
    else
        chrome().client().clearPlaybackControlsManager();
}

void Page::mediaEngineChanged(HTMLMediaElement& mediaElement)
{
    chrome().client().mediaEngineChanged(mediaElement);
}

#endif

void Page::setMuted(MediaProducerMutedStateFlags mutedState)
{
#if ENABLE(MEDIA_STREAM) && ENABLE(MEDIA_SESSION)
    bool cameraCaptureStateDidChange = mutedState.contains(MediaProducerMutedState::VideoCaptureIsMuted) != m_mutedState.contains(MediaProducerMutedState::VideoCaptureIsMuted);
    bool microphoneCaptureStateDidChange = mutedState.contains(MediaProducerMutedState::AudioCaptureIsMuted) != m_mutedState.contains(MediaProducerMutedState::AudioCaptureIsMuted);
    bool screenshareCaptureStateDidChange = (mutedState.contains(MediaProducerMutedState::ScreenCaptureIsMuted) || mutedState.contains(MediaProducerMutedState::WindowCaptureIsMuted)) != (m_mutedState.contains(MediaProducerMutedState::ScreenCaptureIsMuted) || m_mutedState.contains(MediaProducerMutedState::WindowCaptureIsMuted));
#endif

    m_mutedState = mutedState;

    forEachDocument([&] (Document& document) {
#if ENABLE(MEDIA_STREAM) && ENABLE(MEDIA_SESSION)
        if (cameraCaptureStateDidChange)
            document.cameraCaptureStateDidChange();
        if (microphoneCaptureStateDidChange)
            document.microphoneCaptureStateDidChange();
        if (screenshareCaptureStateDidChange)
            document.screenshareCaptureStateDidChange();
#endif
        document.pageMutedStateDidChange();
    });
}

void Page::setShouldSuppressHDR(bool shouldSuppressHDR)
{
    if (m_shouldSuppressHDR == shouldSuppressHDR)
        return;

    m_shouldSuppressHDR = shouldSuppressHDR;
    forEachDocument([](auto& document) {
        document.shouldSuppressHDRDidChange();
    });
}

#if ENABLE(MEDIA_STREAM)
static inline MediaProducerMutedStateFlags toMediaProducerMutedStateFlags(MediaProducerMediaCaptureKind kind)
{
    switch (kind) {
    case MediaProducerMediaCaptureKind::Microphone:
        return MediaProducerMutedState::AudioCaptureIsMuted;
    case MediaProducerMediaCaptureKind::Camera:
        return MediaProducerMutedState::VideoCaptureIsMuted;
    case MediaProducerMediaCaptureKind::Display:
        return { MediaProducerMutedState::ScreenCaptureIsMuted, MediaProducerMutedState::WindowCaptureIsMuted };
    case MediaProducerMediaCaptureKind::SystemAudio:
    case MediaProducerMediaCaptureKind::EveryKind:
        ASSERT_NOT_REACHED();
    }
    return { };
}

static inline MediaProducerMutedStateFlags computeCaptureMutedState(MediaProducerMutedStateFlags currentState, bool isActive, MediaProducerMediaCaptureKind kind)
{
    auto flagsToUpdate = toMediaProducerMutedStateFlags(kind);
    return isActive ? (currentState - flagsToUpdate) : (currentState | flagsToUpdate);
}

void Page::updateCaptureState(bool isActive, MediaProducerMediaCaptureKind kind)
{
    m_mutedState = computeCaptureMutedState(m_mutedState, isActive, kind);
    forEachDocument([&] (Document& document) {
        document.pageMutedStateDidChange();
    });
}

void Page::voiceActivityDetected()
{
    if (auto* controller = UserMediaController::from(this))
        controller->voiceActivityDetected();
}
#endif

void Page::stopMediaCapture(MediaProducerMediaCaptureKind kind)
{
    UNUSED_PARAM(kind);
#if ENABLE(MEDIA_STREAM)
    forEachDocument([kind] (Document& document) {
        document.stopMediaCapture(kind);
    });
#endif
}

bool Page::mediaPlaybackExists()
{
#if ENABLE(VIDEO)
    if (RefPtr sessionManager = mediaSessionManagerIfExists())
        return !sessionManager->hasNoSession();
#endif
    return false;
}

bool Page::mediaPlaybackIsPaused()
{
#if ENABLE(VIDEO)
    if (RefPtr sessionManager = mediaSessionManagerIfExists())
        return sessionManager->mediaPlaybackIsPaused(mediaSessionGroupIdentifier());
#endif
    return false;
}

void Page::pauseAllMediaPlayback()
{
#if ENABLE(VIDEO)
    if (RefPtr sessionManager = mediaSessionManagerIfExists())
        sessionManager->pauseAllMediaPlaybackForGroup(mediaSessionGroupIdentifier());
#endif
}

void Page::suspendAllMediaPlayback()
{
#if ENABLE(VIDEO)
    ASSERT(!m_mediaPlaybackIsSuspended);
    if (m_mediaPlaybackIsSuspended)
        return;

    if (RefPtr sessionManager = mediaSessionManagerIfExists())
        sessionManager->suspendAllMediaPlaybackForGroup(mediaSessionGroupIdentifier());

    // FIXME: We cannot set m_mediaPlaybackIsSuspended before, see https://bugs.webkit.org/show_bug.cgi?id=192829#c7.
    m_mediaPlaybackIsSuspended = true;
#endif
}

std::optional<MediaSessionGroupIdentifier> Page::mediaSessionGroupIdentifier() const
{
    if (!m_mediaSessionGroupIdentifier) {
        if (auto identifier = this->identifier())
            m_mediaSessionGroupIdentifier = ObjectIdentifier<MediaSessionGroupIdentifierType>(identifier->toUInt64());
    }
    return m_mediaSessionGroupIdentifier;
}

void Page::resumeAllMediaPlayback()
{
#if ENABLE(VIDEO)
    ASSERT(m_mediaPlaybackIsSuspended);
    if (!m_mediaPlaybackIsSuspended)
        return;
    m_mediaPlaybackIsSuspended = false;

    if (RefPtr sessionManager = mediaSessionManagerIfExists())
        sessionManager->resumeAllMediaPlaybackForGroup(mediaSessionGroupIdentifier());
#endif
}

void Page::suspendAllMediaBuffering()
{
#if ENABLE(VIDEO)
    ASSERT(!m_mediaBufferingIsSuspended);
    if (m_mediaBufferingIsSuspended)
        return;
    m_mediaBufferingIsSuspended = true;

    if (RefPtr sessionManager = mediaSessionManagerIfExists())
        sessionManager->suspendAllMediaBufferingForGroup(mediaSessionGroupIdentifier());
#endif
}

void Page::resumeAllMediaBuffering()
{
#if ENABLE(VIDEO)
    if (!m_mediaBufferingIsSuspended)
        return;
    m_mediaBufferingIsSuspended = false;

    if (RefPtr sessionManager = mediaSessionManagerIfExists())
        sessionManager->resumeAllMediaBufferingForGroup(mediaSessionGroupIdentifier());
#endif
}

unsigned Page::subframeCount() const
{
    return mainFrame().tree().descendantCount();
}

void Page::resumeAnimatingImages()
{
    // Drawing models which cache painted content while out-of-window (WebKit2's composited drawing areas, etc.)
    // require that we repaint animated images to kickstart the animation loop.
    RefPtr localMainFrame = this->localMainFrame();
    if (RefPtr view = localMainFrame ? localMainFrame->view() : nullptr)
        view->resumeVisibleImageAnimationsIncludingSubframes();
}

void Page::setActivityState(OptionSet<ActivityState> activityState)
{
    auto changed = m_activityState ^ activityState;
    if (!changed)
        return;

    auto oldActivityState = m_activityState;

    bool wasVisibleAndActive = isVisibleAndActive();
    m_activityState = activityState;

    focusController().setActivityState(activityState);

    if (changed & ActivityState::IsVisible)
        setIsVisibleInternal(activityState.contains(ActivityState::IsVisible));
    if (changed & ActivityState::IsInWindow)
        setIsInWindowInternal(activityState.contains(ActivityState::IsInWindow));
    if (changed & ActivityState::IsVisuallyIdle)
        setIsVisuallyIdleInternal(activityState.contains(ActivityState::IsVisuallyIdle));

    WeakPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (changed & ActivityState::WindowIsActive) {
        if (RefPtr view = localMainFrame ? localMainFrame->view() : nullptr)
            view->updateTiledBackingAdaptiveSizing();
    }

    if (changed.containsAny({ActivityState::IsVisible, ActivityState::IsVisuallyIdle, ActivityState::IsAudible, ActivityState::IsLoading, ActivityState::IsCapturingMedia }))
        updateTimerThrottlingState();

    for (auto& observer : m_activityStateChangeObservers)
        observer.activityStateDidChange(oldActivityState, m_activityState);

    if (wasVisibleAndActive != isVisibleAndActive()) {
        mediaSessionManager().updateNowPlayingInfoIfNecessary();
        stopKeyboardScrollAnimation();
    }

    if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr) {
        if (CheckedPtr cache = document->existingAXObjectCache())
            cache->onPageActivityStateChange(m_activityState);
    }

    if (RefPtr performanceMonitor = m_performanceMonitor.get())
        performanceMonitor->activityStateChanged(oldActivityState, activityState);
}

void Page::stopKeyboardScrollAnimation()
{
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        RefPtr frameView = localFrame->view();
        if (!frameView)
            continue;

        frameView->stopKeyboardScrollAnimation();

        auto scrollableAreas = frameView->scrollableAreas();
        if (!scrollableAreas)
            continue;

        for (CheckedRef area : *scrollableAreas) {
            // First call stopAsyncAnimatedScroll() to prepare for the keyboard scroller running on the scrolling thread.
            area->stopAsyncAnimatedScroll();
            area->stopKeyboardScrollAnimation();
        }
    }
}

Ref<DocumentSyncData> Page::protectedTopDocumentSyncData() const
{
    return m_topDocumentSyncData;
}

bool Page::isVisibleAndActive() const
{
    return m_activityState.contains(ActivityState::IsVisible) && m_activityState.contains(ActivityState::WindowIsActive);
}

bool Page::isWindowActive() const
{
    return m_activityState.contains(ActivityState::WindowIsActive);
}

void Page::setIsVisible(bool isVisible)
{
    auto state = m_activityState;

    if (isVisible) {
        state.remove(ActivityState::IsVisuallyIdle);
        state.add({ ActivityState::IsVisible, ActivityState::IsVisibleOrOccluded });
    } else {
        state.add(ActivityState::IsVisuallyIdle);
        state.remove({ ActivityState::IsVisible, ActivityState::IsVisibleOrOccluded });
    }
    setActivityState(state);
}

void Page::setIsVisibleInternal(bool isVisible)
{
    // FIXME: The visibility state should be stored on the top-level document.
    // https://bugs.webkit.org/show_bug.cgi?id=116769

    if (isVisible) {
        m_isPrerender = false;

        resumeScriptedAnimations();

#if PLATFORM(IOS_FAMILY)
        forEachDocument([] (Document& document) {
            document.resumeDeviceMotionAndOrientationUpdates();
        });
#endif

        RefPtr localMainFrame = this->localMainFrame();
        if (RefPtr view = localMainFrame ? localMainFrame->view() : nullptr)
            view->show();

        if (m_settings->hiddenPageCSSAnimationSuspensionEnabled()) {
            forEachDocument([] (Document& document) {
                if (CheckedPtr timelines = document.timelinesController())
                    timelines->resumeAnimations();
            });
        }

        forEachDocument([] (Document& document) {
            if (CheckedPtr svgExtensions = document.svgExtensionsIfExists())
                svgExtensions->unpauseAnimations();
        });

        resumeAnimatingImages();

        if (m_navigationToLogWhenVisible) {
            logNavigation(m_navigationToLogWhenVisible.value());
            m_navigationToLogWhenVisible = std::nullopt;
        }
    }

    if (!isVisible) {
        if (m_settings->hiddenPageCSSAnimationSuspensionEnabled()) {
            forEachDocument([] (Document& document) {
                if (CheckedPtr timelines = document.timelinesController())
                    timelines->suspendAnimations();
            });
        }

        forEachDocument([] (Document& document) {
            if (CheckedPtr svgExtensions = document.svgExtensionsIfExists())
                svgExtensions->pauseAnimations();
        });

#if PLATFORM(IOS_FAMILY)
        forEachDocument([] (Document& document) {
            document.suspendDeviceMotionAndOrientationUpdates();
        });
#endif

        suspendScriptedAnimations();
        RefPtr localMainFrame = this->localMainFrame();
        if (RefPtr view = localMainFrame ? localMainFrame->view() : nullptr)
            view->hide();
    }

    forEachDocument([] (Document& document) {
        document.visibilityStateChanged();
    });
}

void Page::setIsPrerender()
{
    m_isPrerender = true;
    updateDOMTimerAlignmentInterval();
}

VisibilityState Page::visibilityState() const
{
    if (isVisible())
        return VisibilityState::Visible;
    return VisibilityState::Hidden;
}

void Page::setHeaderHeight(int headerHeight)
{
    if (headerHeight == m_headerHeight)
        return;

    m_headerHeight = headerHeight;

    RefPtr localMainFrame = this->localMainFrame();
    RefPtr frameView = localMainFrame ? localMainFrame->view() : nullptr;
    if (!frameView)
        return;

    if (!frameView->renderView())
        return;

    frameView->updateScrollbars(frameView->scrollPosition());
    frameView->setNeedsLayoutAfterViewConfigurationChange();
    frameView->setNeedsCompositingGeometryUpdate();
}

void Page::setFooterHeight(int footerHeight)
{
    if (footerHeight == m_footerHeight)
        return;

    m_footerHeight = footerHeight;

    RefPtr localMainFrame = this->localMainFrame();
    RefPtr frameView = localMainFrame ? localMainFrame->view() : nullptr;
    if (!frameView)
        return;

    if (!frameView->renderView())
        return;

    frameView->updateScrollbars(frameView->scrollPosition());
    frameView->setNeedsLayoutAfterViewConfigurationChange();
    frameView->setNeedsCompositingGeometryUpdate();
}

void Page::setCurrentKeyboardScrollingAnimator(KeyboardScrollingAnimator* animator)
{
    m_currentKeyboardScrollingAnimator = animator;
}

bool Page::shouldApplyScreenFingerprintingProtections(Document& document) const
{
    if (advancedPrivacyProtections().contains(AdvancedPrivacyProtections::FingerprintingProtections))
        return true;

    if (advancedPrivacyProtections().contains(AdvancedPrivacyProtections::ScriptTrackingPrivacy))
        return document.requiresScriptTrackingPrivacyProtection(ScriptTrackingPrivacyCategory::ScreenOrViewport);

    return false;
}

OptionSet<AdvancedPrivacyProtections> Page::advancedPrivacyProtections() const
{
    return protectedMainFrame()->advancedPrivacyProtections();
}

#if ENABLE(REMOTE_INSPECTOR)

bool Page::inspectable() const
{
    return m_inspectorDebuggable->inspectable();
}

void Page::setInspectable(bool inspectable)
{
    m_inspectorDebuggable->setInspectable(inspectable);
}

String Page::remoteInspectionNameOverride() const
{
    return m_inspectorDebuggable->nameOverride();
}

void Page::setRemoteInspectionNameOverride(const String& name)
{
    m_inspectorDebuggable->setNameOverride(name);
}

void Page::remoteInspectorInformationDidChange()
{
    m_inspectorDebuggable->update();
}

#endif

void Page::addLayoutMilestones(OptionSet<LayoutMilestone> milestones)
{
    // In the future, we may want a function that replaces m_layoutMilestones instead of just adding to it.
    m_requestedLayoutMilestones.add(milestones);
}

void Page::removeLayoutMilestones(OptionSet<LayoutMilestone> milestones)
{
    m_requestedLayoutMilestones.remove(milestones);
}

Color Page::themeColor() const
{
    if (RefPtr localTopDocument = this->localTopDocument())
        return localTopDocument->themeColor();

    return { };
}

#if ENABLE(WEB_PAGE_SPATIAL_BACKDROP)
std::optional<SpatialBackdropSource> Page::spatialBackdropSource() const
{
    RefPtr localMainFrame = this->localMainFrame();
    RefPtr document = localMainFrame ? localMainFrame->document() : nullptr;
    if (!document)
        return std::nullopt;

    return document->spatialBackdropSource();
}
#endif

Color Page::pageExtendedBackgroundColor() const
{
    RefPtr localMainFrame = this->localMainFrame();
    RefPtr frameView = localMainFrame ? localMainFrame->view() : nullptr;
    if (!frameView)
        return Color();

    CheckedPtr renderView = frameView->renderView();
    if (!renderView)
        return Color();

    return renderView->compositor().rootExtendedBackgroundColor();
}

Color Page::sampledPageTopColor() const
{
    return valueOrDefault(m_sampledPageTopColor);
}

void Page::computeSampledPageTopColorIfNecessary()
{
    if (m_sampledPageTopColor)
        return;

    m_sampledPageTopColor = PageColorSampler::sampleTop(*this);
    if (m_sampledPageTopColor)
        chrome().client().sampledPageTopColorChanged();
}

void Page::clearSampledPageTopColor()
{
    if (std::exchange(m_sampledPageTopColor, std::nullopt))
        chrome().client().sampledPageTopColorChanged();
}

#if HAVE(APP_ACCENT_COLORS) && PLATFORM(MAC)
void Page::setAppUsesCustomAccentColor(bool appUsesCustomAccentColor)
{
    m_appUsesCustomAccentColor = appUsesCustomAccentColor;
}

bool Page::appUsesCustomAccentColor() const
{
    return m_appUsesCustomAccentColor;
}
#endif

void Page::setUnderPageBackgroundColorOverride(Color&& underPageBackgroundColorOverride)
{
    if (underPageBackgroundColorOverride == m_underPageBackgroundColorOverride)
        return;

    m_underPageBackgroundColorOverride = WTFMove(underPageBackgroundColorOverride);

    scheduleRenderingUpdate({ });

#if HAVE(RUBBER_BANDING)
    RefPtr localMainFrame = this->localMainFrame();
    if (RefPtr frameView = localMainFrame ? localMainFrame->view() : nullptr) {
        if (CheckedPtr renderView = frameView->renderView()) {
            if (renderView->usesCompositing())
                renderView->compositor().updateLayerForOverhangAreasBackgroundColor();
        }
    }
#endif // HAVE(RUBBER_BANDING)
}

// These are magical constants that might be tweaked over time.
static const double gMinimumPaintedAreaRatio = 0.1;
static const double gMaximumUnpaintedAreaRatio = 0.04;

bool Page::isCountingRelevantRepaintedObjects() const
{
    return m_isCountingRelevantRepaintedObjects && m_requestedLayoutMilestones.contains(LayoutMilestone::DidHitRelevantRepaintedObjectsAreaThreshold);
}

void Page::startCountingRelevantRepaintedObjects()
{
    // Reset everything in case we didn't hit the threshold last time.
    resetRelevantPaintedObjectCounter();

    m_isCountingRelevantRepaintedObjects = true;
}

void Page::resetRelevantPaintedObjectCounter()
{
    m_isCountingRelevantRepaintedObjects = false;
    m_relevantUnpaintedRenderObjects.clear();
    m_internals->topRelevantPaintedRegion = Region();
    m_internals->bottomRelevantPaintedRegion = Region();
    m_internals->relevantUnpaintedRegion = Region();
}

static LayoutRect relevantViewRect(RenderView* view)
{
    LayoutRect viewRect = view->viewRect();

    float relevantViewRectWidth = 980;
#if PLATFORM(WATCHOS)
    // FIXME(186051): Consider limiting the relevant rect width to the view width everywhere.
    relevantViewRectWidth = std::min<float>(viewRect.width().toFloat(), relevantViewRectWidth);
#endif

    // DidHitRelevantRepaintedObjectsAreaThreshold is a LayoutMilestone intended to indicate that
    // a certain relevant amount of content has been drawn to the screen. This is the rect that
    // has been determined to be relevant in the context of this goal. We may choose to tweak
    // the rect over time, much like we may choose to tweak gMinimumPaintedAreaRatio and
    // gMaximumUnpaintedAreaRatio. But this seems to work well right now.
    LayoutRect relevantViewRect { 0, 0, LayoutUnit(relevantViewRectWidth), 1300 };
    // If the viewRect is wider than the relevantViewRect, center the relevantViewRect.
    if (viewRect.width() > relevantViewRect.width())
        relevantViewRect.setX((viewRect.width() - relevantViewRect.width()) / 2);

    return relevantViewRect;
}

void Page::addRelevantRepaintedObject(const RenderObject& object, const LayoutRect& objectPaintRect)
{
    if (!isCountingRelevantRepaintedObjects())
        return;

    // Objects inside sub-frames are not considered to be relevant.
    if (&object.frame() != &mainFrame())
        return;

    LayoutRect relevantRect = relevantViewRect(&object.view());

    // The objects are only relevant if they are being painted within the viewRect().
    if (!objectPaintRect.intersects(snappedIntRect(relevantRect)))
        return;

    IntRect snappedPaintRect = snappedIntRect(objectPaintRect);

    // If this object was previously counted as an unpainted object, remove it from that HashSet
    // and corresponding Region. FIXME: This doesn't do the right thing if the objects overlap.
    if (m_relevantUnpaintedRenderObjects.remove(object))
        m_internals->relevantUnpaintedRegion.subtract(snappedPaintRect);

    // Split the relevantRect into a top half and a bottom half. Making sure we have coverage in
    // both halves helps to prevent cases where we have a fully loaded menu bar or masthead with
    // no content beneath that.
    LayoutRect topRelevantRect = relevantRect;
    topRelevantRect.contract(LayoutSize(0_lu, relevantRect.height() / 2));
    LayoutRect bottomRelevantRect = topRelevantRect;
    bottomRelevantRect.setY(relevantRect.height() / 2);

    // If the rect straddles both Regions, split it appropriately.
    if (topRelevantRect.intersects(snappedPaintRect) && bottomRelevantRect.intersects(snappedPaintRect)) {
        IntRect topIntersection = snappedPaintRect;
        topIntersection.intersect(snappedIntRect(topRelevantRect));
        m_internals->topRelevantPaintedRegion.unite(topIntersection);

        IntRect bottomIntersection = snappedPaintRect;
        bottomIntersection.intersect(snappedIntRect(bottomRelevantRect));
        m_internals->bottomRelevantPaintedRegion.unite(bottomIntersection);
    } else if (topRelevantRect.intersects(snappedPaintRect))
        m_internals->topRelevantPaintedRegion.unite(snappedPaintRect);
    else
        m_internals->bottomRelevantPaintedRegion.unite(snappedPaintRect);

    float topPaintedArea = m_internals->topRelevantPaintedRegion.totalArea();
    float bottomPaintedArea = m_internals->bottomRelevantPaintedRegion.totalArea();
    float viewArea = relevantRect.width() * relevantRect.height();

    float ratioThatIsPaintedOnTop = topPaintedArea / viewArea;
    float ratioThatIsPaintedOnBottom = bottomPaintedArea / viewArea;
    float ratioOfViewThatIsUnpainted = m_internals->relevantUnpaintedRegion.totalArea() / viewArea;

    if (ratioThatIsPaintedOnTop > (gMinimumPaintedAreaRatio / 2) && ratioThatIsPaintedOnBottom > (gMinimumPaintedAreaRatio / 2)
        && ratioOfViewThatIsUnpainted < gMaximumUnpaintedAreaRatio) {
        m_isCountingRelevantRepaintedObjects = false;
        resetRelevantPaintedObjectCounter();
        if (RefPtr frame = dynamicDowncast<LocalFrame>(mainFrame()))
            frame->loader().didReachLayoutMilestone(LayoutMilestone::DidHitRelevantRepaintedObjectsAreaThreshold);
    }
}

void Page::addRelevantUnpaintedObject(const RenderObject& object, const LayoutRect& objectPaintRect)
{
    if (!isCountingRelevantRepaintedObjects())
        return;

    // The objects are only relevant if they are being painted within the relevantViewRect().
    if (!objectPaintRect.intersects(snappedIntRect(relevantViewRect(&object.view()))))
        return;

    m_relevantUnpaintedRenderObjects.add(object);
    m_internals->relevantUnpaintedRegion.unite(snappedIntRect(objectPaintRect));
}

void Page::suspendActiveDOMObjectsAndAnimations()
{
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame))
            localFrame->suspendActiveDOMObjectsAndAnimations();
    }
}

void Page::resumeActiveDOMObjectsAndAnimations()
{
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame))
            localFrame->resumeActiveDOMObjectsAndAnimations();
    }

    resumeAnimatingImages();
}

bool Page::hasSeenAnyPlugin() const
{
    return !m_seenPlugins.isEmpty();
}

bool Page::hasSeenPlugin(const String& serviceType) const
{
    return m_seenPlugins.contains(serviceType);
}

void Page::sawPlugin(const String& serviceType)
{
    m_seenPlugins.add(serviceType);
}

void Page::resetSeenPlugins()
{
    m_seenPlugins.clear();
}

bool Page::hasSeenAnyMediaEngine() const
{
    return !m_seenMediaEngines.isEmpty();
}

bool Page::hasSeenMediaEngine(const String& engineDescription) const
{
    return m_seenMediaEngines.contains(engineDescription);
}

void Page::sawMediaEngine(const String& engineDescription)
{
    m_seenMediaEngines.add(engineDescription);
}

void Page::resetSeenMediaEngines()
{
    m_seenMediaEngines.clear();
}

void Page::hiddenPageCSSAnimationSuspensionStateChanged()
{
    if (!isVisible()) {
        forEachDocument([&] (Document& document) {
            if (CheckedPtr timelines = document.timelinesController()) {
                if (m_settings->hiddenPageCSSAnimationSuspensionEnabled())
                    timelines->suspendAnimations();
                else
                    timelines->resumeAnimations();
            }
        });
    }
}

#if ENABLE(VIDEO)

void Page::captionPreferencesChanged()
{
    forEachDocument([] (Document& document) {
        document.captionPreferencesChanged();
    });
}

#endif

void Page::forbidPrompts()
{
    ++m_forbidPromptsDepth;
}

void Page::allowPrompts()
{
    ASSERT(m_forbidPromptsDepth);
    --m_forbidPromptsDepth;
}

bool Page::arePromptsAllowed()
{
    return !m_forbidPromptsDepth;
}

void Page::forbidSynchronousLoads()
{
    ++m_forbidSynchronousLoadsDepth;
}

void Page::allowSynchronousLoads()
{
    ASSERT(m_forbidSynchronousLoadsDepth);
    --m_forbidSynchronousLoadsDepth;
}

bool Page::areSynchronousLoadsAllowed()
{
    return !m_forbidSynchronousLoadsDepth;
}

void Page::logNavigation(const Navigation& navigation)
{
    String navigationDescription;
    switch (navigation.type) {
    case FrameLoadType::Standard:
        navigationDescription = "standard"_s;
        break;
    case FrameLoadType::Back:
        navigationDescription = "back"_s;
        break;
    case FrameLoadType::Forward:
        navigationDescription = "forward"_s;
        break;
    case FrameLoadType::IndexedBackForward:
        navigationDescription = "indexedBackForward"_s;
        break;
    case FrameLoadType::Reload:
        navigationDescription = "reload"_s;
        break;
    case FrameLoadType::Same:
        navigationDescription = "same"_s;
        break;
    case FrameLoadType::ReloadFromOrigin:
        navigationDescription = "reloadFromOrigin"_s;
        break;
    case FrameLoadType::ReloadExpiredOnly:
        navigationDescription = "reloadRevalidatingExpired"_s;
        break;
    case FrameLoadType::Replace:
    case FrameLoadType::RedirectWithLockedBackForwardList:
        // Not logging those for now.
        return;
    }
    diagnosticLoggingClient().logDiagnosticMessage(DiagnosticLoggingKeys::navigationKey(), navigationDescription, ShouldSample::No);

    if (!navigation.domain.isEmpty())
        diagnosticLoggingClient().logDiagnosticMessageWithEnhancedPrivacy(DiagnosticLoggingKeys::domainVisitedKey(), navigation.domain.string(), ShouldSample::Yes);
}

void Page::mainFrameLoadStarted(const URL& destinationURL, FrameLoadType type)
{
    Navigation navigation = { RegistrableDomain { destinationURL }, type };

    // To avoid being too verbose, we only log navigations if the page is or becomes visible. This avoids logging non-user observable loads.
    if (!isVisible()) {
        m_navigationToLogWhenVisible = navigation;
        return;
    }

    m_navigationToLogWhenVisible = std::nullopt;
    logNavigation(navigation);
}

Ref<CookieJar> Page::protectedCookieJar() const
{
    return m_cookieJar;
}

Ref<StorageNamespaceProvider> Page::protectedStorageNamespaceProvider() const
{
    return m_storageNamespaceProvider;
}

PluginInfoProvider& Page::pluginInfoProvider()
{
    return m_pluginInfoProvider;
}

Ref<PluginInfoProvider> Page::protectedPluginInfoProvider() const
{
    return m_pluginInfoProvider;
}

UserContentProvider& Page::userContentProvider()
{
    return m_userContentProvider;
}

Ref<UserContentProvider> Page::protectedUserContentProvider()
{
    return m_userContentProvider;
}

void Page::setUserContentProvider(Ref<UserContentProvider>&& userContentProvider)
{
    protectedUserContentProvider()->removePage(*this);
    m_userContentProvider = WTFMove(userContentProvider);
    protectedUserContentProvider()->addPage(*this);

    invalidateInjectedStyleSheetCacheInAllFrames();
}

VisitedLinkStore& Page::visitedLinkStore()
{
    return m_visitedLinkStore;
}

Ref<VisitedLinkStore> Page::protectedVisitedLinkStore()
{
    return m_visitedLinkStore;
}

void Page::setVisitedLinkStore(Ref<VisitedLinkStore>&& visitedLinkStore)
{
    protectedVisitedLinkStore()->removePage(*this);
    m_visitedLinkStore = WTFMove(visitedLinkStore);
    protectedVisitedLinkStore()->addPage(*this);

    invalidateStylesForAllLinks();
}

std::optional<uint64_t> Page::noiseInjectionHashSaltForDomain(const RegistrableDomain& domain)
{
    if (!m_noiseInjectionHashSalts.isValidKey(domain))
        return std::nullopt;

    return m_noiseInjectionHashSalts.ensure(domain, [] {
        return cryptographicallyRandomNumber<uint64_t>();
    }).iterator->value;
}

PAL::SessionID Page::sessionID() const
{
    return m_sessionID;
}

// This is only called by WebKitLegacy.
void Page::setSessionID(PAL::SessionID sessionID)
{
    ASSERT(sessionID.isValid());
    ASSERT(m_sessionID == PAL::SessionID::legacyPrivateSessionID() || m_sessionID == PAL::SessionID::defaultSessionID());
    ASSERT(sessionID == PAL::SessionID::legacyPrivateSessionID() || sessionID == PAL::SessionID::defaultSessionID());

    if (sessionID != m_sessionID)
        m_idbConnectionToServer = nullptr;

    if (sessionID != m_sessionID) {
        constexpr auto doNotCreate = StorageNamespaceProvider::ShouldCreateNamespace::No;
        RefPtr topOrigin = mainFrameOrigin();
        if (RefPtr sessionStorage = topOrigin ? m_storageNamespaceProvider->sessionStorageNamespace(*topOrigin, *this, doNotCreate) : nullptr)
            sessionStorage->setSessionIDForTesting(sessionID);
    }

    bool privateBrowsingStateChanged = (sessionID.isEphemeral() != m_sessionID.isEphemeral());

    m_sessionID = sessionID;

    if (!privateBrowsingStateChanged)
        return;

    forEachDocument([&] (Document& document) {
        document.privateBrowsingStateDidChange(m_sessionID);
    });
}

#if ENABLE(WIRELESS_PLAYBACK_TARGET)
void Page::addPlaybackTargetPickerClient(PlaybackTargetClientContextIdentifier contextId)
{
    chrome().client().addPlaybackTargetPickerClient(contextId);
}

void Page::removePlaybackTargetPickerClient(PlaybackTargetClientContextIdentifier contextId)
{
    chrome().client().removePlaybackTargetPickerClient(contextId);
}

void Page::showPlaybackTargetPicker(PlaybackTargetClientContextIdentifier contextId, const WebCore::IntPoint& location, bool isVideo, RouteSharingPolicy routeSharingPolicy, const String& routingContextUID)
{
#if PLATFORM(IOS_FAMILY)
    // FIXME: refactor iOS implementation.
    UNUSED_PARAM(contextId);
    UNUSED_PARAM(location);
    chrome().client().showPlaybackTargetPicker(isVideo, routeSharingPolicy, routingContextUID);
#else
    UNUSED_PARAM(routeSharingPolicy);
    UNUSED_PARAM(routingContextUID);
    chrome().client().showPlaybackTargetPicker(contextId, location, isVideo);
#endif
}

void Page::playbackTargetPickerClientStateDidChange(PlaybackTargetClientContextIdentifier contextId, MediaProducerMediaStateFlags state)
{
    chrome().client().playbackTargetPickerClientStateDidChange(contextId, state);
}

void Page::setMockMediaPlaybackTargetPickerEnabled(bool enabled)
{
    chrome().client().setMockMediaPlaybackTargetPickerEnabled(enabled);
}

void Page::setMockMediaPlaybackTargetPickerState(const String& name, MediaPlaybackTargetContext::MockState state)
{
    chrome().client().setMockMediaPlaybackTargetPickerState(name, state);
}

void Page::mockMediaPlaybackTargetPickerDismissPopup()
{
    chrome().client().mockMediaPlaybackTargetPickerDismissPopup();
}

void Page::setPlaybackTarget(PlaybackTargetClientContextIdentifier contextId, Ref<MediaPlaybackTarget>&& target)
{
    forEachDocument([&] (Document& document) {
        document.setPlaybackTarget(contextId, target.copyRef());
    });
}

void Page::playbackTargetAvailabilityDidChange(PlaybackTargetClientContextIdentifier contextId, bool available)
{
    forEachDocument([&] (Document& document) {
        document.playbackTargetAvailabilityDidChange(contextId, available);
    });
}

void Page::setShouldPlayToPlaybackTarget(PlaybackTargetClientContextIdentifier contextId, bool shouldPlay)
{
    forEachDocument([&] (Document& document) {
        document.setShouldPlayToPlaybackTarget(contextId, shouldPlay);
    });
}

void Page::playbackTargetPickerWasDismissed(PlaybackTargetClientContextIdentifier contextId)
{
    forEachDocument([&] (Document& document) {
        document.playbackTargetPickerWasDismissed(contextId);
    });
}

#endif

RefPtr<WheelEventTestMonitor> Page::wheelEventTestMonitor() const
{
    return m_wheelEventTestMonitor;
}

void Page::clearWheelEventTestMonitor()
{
    if (RefPtr scrollingCoordinator = m_scrollingCoordinator)
        scrollingCoordinator->stopMonitoringWheelEvents();

    m_wheelEventTestMonitor = nullptr;
}

bool Page::isMonitoringWheelEvents() const
{
    return !!m_wheelEventTestMonitor;
}

void Page::startMonitoringWheelEvents(bool clearLatchingState)
{
    ensureProtectedWheelEventTestMonitor()->clearAllTestDeferrals();

#if ENABLE(WHEEL_EVENT_LATCHING)
    if (clearLatchingState)
        protectedScrollLatchingController()->clear();
#endif

    RefPtr localMainFrame = this->localMainFrame();
    if (RefPtr frameView = localMainFrame ? localMainFrame->view() : nullptr) {
        if (RefPtr scrollingCoordinator = m_scrollingCoordinator) {
            scrollingCoordinator->startMonitoringWheelEvents(clearLatchingState);
            scrollingCoordinator->updateIsMonitoringWheelEventsForFrameView(*frameView);
        }
    }
}

WheelEventTestMonitor& Page::ensureWheelEventTestMonitor()
{
    if (!m_wheelEventTestMonitor)
        m_wheelEventTestMonitor = adoptRef(new WheelEventTestMonitor(*this));

    return *m_wheelEventTestMonitor;
}

Ref<WheelEventTestMonitor> Page::ensureProtectedWheelEventTestMonitor()
{
    return ensureWheelEventTestMonitor();
}

#if ENABLE(VIDEO)

void Page::setAllowsMediaDocumentInlinePlayback(bool flag)
{
    if (m_allowsMediaDocumentInlinePlayback == flag)
        return;
    m_allowsMediaDocumentInlinePlayback = flag;

    forEachMediaElement([] (HTMLMediaElement& element) {
        element.allowsMediaDocumentInlinePlaybackChanged();
    });
}

#endif

IDBClient::IDBConnectionToServer& Page::idbConnection()
{
    if (!m_idbConnectionToServer)
        m_idbConnectionToServer = m_databaseProvider->idbConnectionToServerForSession(m_sessionID);
    
    return *m_idbConnectionToServer;
}

IDBClient::IDBConnectionToServer* Page::optionalIDBConnection()
{
    return m_idbConnectionToServer.get();
}

void Page::clearIDBConnection()
{
    m_idbConnectionToServer = nullptr;
}

#if ENABLE(RESOURCE_USAGE)
void Page::setResourceUsageOverlayVisible(bool visible)
{
    if (!visible) {
        m_resourceUsageOverlay = nullptr;
        return;
    }

    if (!m_resourceUsageOverlay && m_settings->acceleratedCompositingEnabled())
        m_resourceUsageOverlay = ResourceUsageOverlay::create(*this);
}
#endif

String Page::captionUserPreferencesStyleSheet()
{
    return m_captionUserPreferencesStyleSheet;
}

void Page::setCaptionUserPreferencesStyleSheet(const String& styleSheet)
{
    if (m_captionUserPreferencesStyleSheet == styleSheet)
        return;

    m_captionUserPreferencesStyleSheet = styleSheet;
}

void Page::accessibilitySettingsDidChange()
{
    forEachDocument([] (auto& document) {
        document.styleScope().evaluateMediaQueriesForAccessibilitySettingsChange();
        document.updateElementsAffectedByMediaQueries();
        document.scheduleRenderingUpdate(RenderingUpdateStep::MediaQueryEvaluation);
    });

    InspectorInstrumentation::accessibilitySettingsDidChange(*this);
}

void Page::appearanceDidChange()
{
    forEachDocument([] (auto& document) {
        document.styleScope().didChangeStyleSheetEnvironment();
        document.styleScope().evaluateMediaQueriesForAppearanceChange();
        document.updateElementsAffectedByMediaQueries();
        document.scheduleRenderingUpdate(RenderingUpdateStep::MediaQueryEvaluation);
        document.invalidateScrollbars();
    });
}

void Page::clearAXObjectCache()
{
    m_axObjectCache = nullptr;
}

AXObjectCache* Page::axObjectCache()
{
    if (!m_axObjectCache) {
        RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
        RefPtr mainFrameDocument = localMainFrame ? localMainFrame->document() : nullptr;
        if (mainFrameDocument && !mainFrameDocument->hasLivingRenderTree())
            return nullptr;
        m_axObjectCache = makeUnique<AXObjectCache>(*this, mainFrameDocument.get());
        Document::hasEverCreatedAnAXObjectCache = true;
    }
    return m_axObjectCache.get();
}

void Page::setUnobscuredSafeAreaInsets(const FloatBoxExtent& insets)
{
    if (m_unobscuredSafeAreaInsets == insets)
        return;

    m_unobscuredSafeAreaInsets = insets;

    forEachDocument([&] (Document& document) {
        document.constantProperties().didChangeSafeAreaInsets();
    });
}

void Page::useSystemAppearanceChanged()
{
    appearanceDidChange();

    forEachDocument([&](Document& document) {
        // System appearance change may affect stylesheet parsing. We need to re-parse.
        if (CheckedPtr extensionStyleSheets = document.extensionStyleSheetsIfExists()) {
            extensionStyleSheets->clearPageUserSheet();
            extensionStyleSheets->invalidateInjectedStyleSheetCache();
        }
    });
}

void Page::setUseColorAppearance(bool useDarkAppearance, bool useElevatedUserInterfaceLevel)
{
#if ENABLE(DARK_MODE_CSS)
    if (m_useDarkAppearance == useDarkAppearance && m_useElevatedUserInterfaceLevel == useElevatedUserInterfaceLevel)
        return;

    m_useDarkAppearance = useDarkAppearance;
    m_useElevatedUserInterfaceLevel = useElevatedUserInterfaceLevel;

    InspectorInstrumentation::defaultAppearanceDidChange(*this);

    appearanceDidChange();
#else
    UNUSED_PARAM(useDarkAppearance);

    if (m_useElevatedUserInterfaceLevel == useElevatedUserInterfaceLevel)
        return;

    m_useElevatedUserInterfaceLevel = useElevatedUserInterfaceLevel;

    appearanceDidChange();
#endif
}

bool Page::useDarkAppearance() const
{
#if ENABLE(DARK_MODE_CSS)
    RefPtr localMainFrame = this->localMainFrame();

    // FIXME: If this page is being printed, this function should return false.
    // Currently remote mainFrame() does not have this information.
    if (!localMainFrame)
        return m_useDarkAppearance;

    RefPtr view = localMainFrame->view();
    if (!view || view->mediaType() != screenAtom())
        return false;

    if (m_useDarkAppearanceOverride)
        return m_useDarkAppearanceOverride.value();

    if (RefPtr documentLoader = localMainFrame->loader().documentLoader()) {
        auto colorSchemePreference = documentLoader->colorSchemePreference();
        if (colorSchemePreference != ColorSchemePreference::NoPreference)
            return colorSchemePreference == ColorSchemePreference::Dark;
    }

    return m_useDarkAppearance;
#else
    return false;
#endif
}

void Page::setUseDarkAppearanceOverride(std::optional<bool> valueOverride)
{
    if (valueOverride == m_useDarkAppearanceOverride)
        return;

    m_useDarkAppearanceOverride = valueOverride;

    appearanceDidChange();
}

void Page::setFullscreenInsets(const FloatBoxExtent& insets)
{
    if (insets == m_fullscreenInsets)
        return;

    m_fullscreenInsets = insets;

    forEachDocument([] (Document& document) {
        document.constantProperties().didChangeFullscreenInsets();
    });
}

void Page::setFullscreenAutoHideDuration(Seconds duration)
{
    if (duration == m_fullscreenAutoHideDuration)
        return;

    m_fullscreenAutoHideDuration = duration;

    forEachDocument([&] (Document& document) {
        document.constantProperties().setFullscreenAutoHideDuration(duration);
    });
}

Document* Page::outermostFullscreenDocument() const
{
#if ENABLE(FULLSCREEN_API)
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (!localMainFrame)
        return nullptr;

    RefPtr<Document> outermostFullscreenDocument;
    RefPtr currentDocument = localMainFrame->document();
    while (currentDocument) {
        RefPtr fullscreenElement = currentDocument->protectedFullscreen()->fullscreenElement();
        if (!fullscreenElement)
            break;

        outermostFullscreenDocument = currentDocument;
        RefPtr fullscreenFrame = dynamicDowncast<HTMLFrameOwnerElement>(fullscreenElement.releaseNonNull());
        if (!fullscreenFrame)
            break;

        currentDocument = fullscreenFrame->contentDocument();
    }
    return outermostFullscreenDocument.get();
#else
    return nullptr;
#endif
}

void Page::disableICECandidateFiltering()
{
    m_shouldEnableICECandidateFilteringByDefault = false;
#if ENABLE(WEB_RTC)
    m_rtcController->disableICECandidateFilteringForAllOrigins();
#endif
}

void Page::enableICECandidateFiltering()
{
    m_shouldEnableICECandidateFilteringByDefault = true;
#if ENABLE(WEB_RTC)
    m_rtcController->enableICECandidateFiltering();
#endif
}

RefPtr<LocalFrame> Page::localMainFrame()
{
    return dynamicDowncast<LocalFrame>(mainFrame());
}

RefPtr<const LocalFrame> Page::localMainFrame() const
{
    return dynamicDowncast<LocalFrame>(mainFrame());
}

RefPtr<Document> Page::localTopDocument()
{
    if (RefPtr localMainFrame = this->localMainFrame())
        return localMainFrame->document();
    return nullptr;
}

RefPtr<Document> Page::localTopDocument() const
{
    if (RefPtr localMainFrame = this->localMainFrame())
        return localMainFrame->document();
    return nullptr;
}

bool Page::hasLocalMainFrame()
{
    return dynamicDowncast<LocalFrame>(mainFrame());
}

void Page::didChangeMainDocument(Document* newDocument)
{
    m_topDocumentSyncData = newDocument ? newDocument->syncData() : DocumentSyncData::create();

    if (settings().siteIsolationEnabled())
        processSyncClient().broadcastTopDocumentSyncDataToOtherProcesses(protectedTopDocumentSyncData().get());

#if ENABLE(WEB_RTC)
    m_rtcController->reset(m_shouldEnableICECandidateFilteringByDefault);
#endif
    m_pointerCaptureController->reset();

    clearSampledPageTopColor();

    checkedElementTargetingController()->didChangeMainDocument(newDocument);

    updateActiveNowPlayingSessionNow();
}

RenderingUpdateScheduler& Page::renderingUpdateScheduler()
{
    if (!m_renderingUpdateScheduler)
        m_renderingUpdateScheduler = RenderingUpdateScheduler::create(*this);
    return *m_renderingUpdateScheduler;
}

RenderingUpdateScheduler* Page::existingRenderingUpdateScheduler()
{
    return m_renderingUpdateScheduler.get();
}

void Page::forEachDocumentFromMainFrame(const Frame& mainFrame, NOESCAPE const Function<void(Document&)>& functor)
{
    Vector<Ref<Document>, 8> documents;
    for (RefPtr frame = mainFrame; frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        if (RefPtr document = localFrame->document())
            documents.append(document.releaseNonNull());
    }

    for (auto& document : documents)
        functor(document);
}

void Page::forEachDocument(NOESCAPE const Function<void(Document&)>& functor) const
{
    forEachDocumentFromMainFrame(protectedMainFrame(), functor);
}

bool Page::findMatchingLocalDocument(NOESCAPE const Function<bool(Document&)>& functor) const
{
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        RefPtr document = localFrame->document();
        if (!document)
            continue;
        if (functor(*document))
            return true;
    }
    return false;
}

void Page::forEachRenderableDocument(NOESCAPE const Function<void(Document&)>& functor) const
{
    Vector<Ref<Document>, 8> documents;
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        RefPtr document = localFrame->document();
        if (!document)
            continue;
        if (document->renderingIsSuppressedForViewTransition())
            continue;
        if (!document->visualUpdatesAllowed())
            continue;
        documents.append(document.releaseNonNull());
    }
    for (auto& document : documents)
        functor(document);
}

void Page::forEachMediaElement(NOESCAPE const Function<void(HTMLMediaElement&)>& functor)
{
#if ENABLE(VIDEO)
    forEachDocument([&] (Document& document) {
        document.forEachMediaElement(functor);
    });
#else
    UNUSED_PARAM(functor);
#endif
}

void Page::forEachLocalFrame(NOESCAPE const Function<void(LocalFrame&)>& functor)
{
    Vector<Ref<LocalFrame>> frames;
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame))
            frames.append(localFrame.releaseNonNull());
    }

    for (auto& frame : frames)
        functor(frame);
}

void Page::forEachWindowEventLoop(NOESCAPE const Function<void(WindowEventLoop&)>& functor)
{
    HashSet<Ref<WindowEventLoop>> windowEventLoops;
    RefPtr<WindowEventLoop> lastEventLoop;
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        RefPtr document = localFrame->document();
        if (!document)
            continue;
        Ref currentEventLoop = document->windowEventLoop();
        if (lastEventLoop == currentEventLoop.ptr())
            continue; // Common and faster than a hash table lookup
        lastEventLoop = currentEventLoop.ptr();
        windowEventLoops.add(WTFMove(currentEventLoop));
    }
    for (auto& eventLoop : windowEventLoops)
        functor(eventLoop);
}

bool Page::allowsLoadFromURL(const URL& url, MainFrameMainResource mainFrameMainResource) const
{
    if (mainFrameMainResource == MainFrameMainResource::No && !m_loadsSubresources)
        return false;
    if (!m_allowedNetworkHosts)
        return true;
    if (!url.protocolIsInHTTPFamily() && !url.protocolIs("ws"_s) && !url.protocolIs("wss"_s))
        return true;
    return m_allowedNetworkHosts->contains<StringViewHashTranslator>(url.host());
}

bool Page::hasLocalDataForURL(const URL& url)
{
    if (url.protocolIsFile())
        return true;

    RefPtr localMainFrame = this->localMainFrame();
    RefPtr documentLoader = localMainFrame ? localMainFrame->loader().documentLoader() : nullptr;
    if (documentLoader && documentLoader->subresource(MemoryCache::removeFragmentIdentifierIfNeeded(url)))
        return true;

    return false;
}

void Page::applicationWillResignActive()
{
#if ENABLE(VIDEO)
    forEachMediaElement([] (HTMLMediaElement& element) {
        element.applicationWillResignActive();
    });
#endif
}

void Page::applicationDidEnterBackground()
{
#if ENABLE(WEBXR)
    if (auto session = this->activeImmersiveXRSession())
        session->applicationDidEnterBackground();
#endif
}

void Page::applicationWillEnterForeground()
{
#if ENABLE(WEBXR)
    if (auto session = this->activeImmersiveXRSession())
        session->applicationWillEnterForeground();
#endif
}

void Page::applicationDidBecomeActive()
{
#if ENABLE(VIDEO)
    forEachMediaElement([] (HTMLMediaElement& element) {
        element.applicationDidBecomeActive();
    });
#endif
}

#if ENABLE(WHEEL_EVENT_LATCHING)
ScrollLatchingController& Page::scrollLatchingController()
{
    if (!m_scrollLatchingController)
        lazyInitialize(m_scrollLatchingController, makeUniqueWithoutRefCountedCheck<ScrollLatchingController>(*this));
        
    return *m_scrollLatchingController;
}

Ref<ScrollLatchingController> Page::protectedScrollLatchingController()
{
    return scrollLatchingController();
}
#endif // ENABLE(WHEEL_EVENT_LATCHING)

enum class DispatchedOnDocumentEventLoop : bool { No, Yes };
static void dispatchPrintEvent(Frame& mainFrame, const AtomString& eventType, DispatchedOnDocumentEventLoop dispatchedOnDocumentEventLoop)
{
    Vector<Ref<LocalFrame>> frames;
    for (RefPtr frame = mainFrame; frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        frames.append(localFrame.releaseNonNull());
    }

    for (auto& frame : frames) {
        if (RefPtr window = frame->window()) {
            auto dispatchEvent = [window = WTFMove(window), eventType] {
                window->dispatchEvent(Event::create(eventType, Event::CanBubble::No, Event::IsCancelable::No), window->protectedDocument().get());
            };
            if (dispatchedOnDocumentEventLoop == DispatchedOnDocumentEventLoop::No)
                return dispatchEvent();
            if (RefPtr document = frame->document())
                document->checkedEventLoop()->queueTask(TaskSource::DOMManipulation, WTFMove(dispatchEvent));
        }
    }
}

void Page::dispatchBeforePrintEvent()
{
    dispatchPrintEvent(m_mainFrame, eventNames().beforeprintEvent, DispatchedOnDocumentEventLoop::No);
}

void Page::dispatchAfterPrintEvent()
{
    dispatchPrintEvent(m_mainFrame, eventNames().afterprintEvent, DispatchedOnDocumentEventLoop::Yes);
}

#if ENABLE(APPLE_PAY)
Ref<PaymentCoordinator> Page::protectedPaymentCoordinator() const
{
    return paymentCoordinator();
}

void Page::setPaymentCoordinator(Ref<PaymentCoordinator>&& paymentCoordinator)
{
    m_paymentCoordinator = WTFMove(paymentCoordinator);
}
#endif

#if ENABLE(APPLE_PAY_AMS_UI)

bool Page::startApplePayAMSUISession(const URL& originatingURL, ApplePayAMSUIPaymentHandler& paymentHandler, const ApplePayAMSUIRequest& request)
{
    if (hasActiveApplePayAMSUISession())
        return false;

    m_activeApplePayAMSUIPaymentHandler = paymentHandler;

    chrome().client().startApplePayAMSUISession(originatingURL, request, [weakThis = WeakPtr { *this }, paymentHandler = Ref { paymentHandler }] (std::optional<bool>&& result) {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return;

        if (paymentHandler.ptr() != protectedThis->m_activeApplePayAMSUIPaymentHandler)
            return;

        if (auto activePaymentHandler = std::exchange(protectedThis->m_activeApplePayAMSUIPaymentHandler, nullptr))
            activePaymentHandler->finishSession(WTFMove(result));
    });
    return true;
}

void Page::abortApplePayAMSUISession(ApplePayAMSUIPaymentHandler& paymentHandler)
{
    if (&paymentHandler != m_activeApplePayAMSUIPaymentHandler)
        return;

    chrome().client().abortApplePayAMSUISession();

    if (auto activePaymentHandler = std::exchange(m_activeApplePayAMSUIPaymentHandler, nullptr))
        activePaymentHandler->finishSession(std::nullopt);
}

#endif // ENABLE(APPLE_PAY_AMS_UI)

#if USE(SYSTEM_PREVIEW)
void Page::beginSystemPreview(const URL& url, const SecurityOriginData& topOrigin, const SystemPreviewInfo& systemPreviewInfo, CompletionHandler<void()>&& completionHandler)
{
    chrome().client().beginSystemPreview(url, topOrigin, systemPreviewInfo, WTFMove(completionHandler));
}
#endif

#if ENABLE(MEDIA_SESSION_COORDINATOR)
void Page::setMediaSessionCoordinator(Ref<MediaSessionCoordinatorPrivate>&& mediaSessionCoordinator)
{
    m_mediaSessionCoordinator = WTFMove(mediaSessionCoordinator);

    RefPtr localMainFrame = this->localMainFrame();
    RefPtr window = localMainFrame ? localMainFrame->window() : nullptr;
    if (RefPtr navigator = window ? window->optionalNavigator() : nullptr)
        NavigatorMediaSession::mediaSession(*navigator).coordinator().setMediaSessionCoordinatorPrivate(*m_mediaSessionCoordinator);
}

void Page::invalidateMediaSessionCoordinator()
{
    m_mediaSessionCoordinator = nullptr;
    RefPtr localMainFrame = this->localMainFrame();
    RefPtr window = localMainFrame ? localMainFrame->window() : nullptr;
    if (!window)
        return;

    RefPtr navigator = window->optionalNavigator();
    if (!navigator)
        return;

    NavigatorMediaSession::mediaSession(*navigator).coordinator().close();
}
#endif

void Page::configureLoggingChannel(const String& channelName, WTFLogChannelState state, WTFLogLevel level)
{
#if !RELEASE_LOG_DISABLED
    if (auto* channel = getLogChannel(channelName)) {
        channel->state = state;
        channel->level = level;

#if USE(LIBWEBRTC)
        RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
        if (channel == &LogWebRTC && localMainFrame && localMainFrame->document() && isAlwaysOnLoggingAllowed() && (m_settings->webCodecsVideoEnabled() || m_settings->peerConnectionEnabled()))
            webRTCProvider().setLoggingLevel(LogWebRTC.level);
#endif
    }

    chrome().client().configureLoggingChannel(channelName, state, level);
#else
    UNUSED_PARAM(channelName);
    UNUSED_PARAM(state);
    UNUSED_PARAM(level);
#endif
}

void Page::didFinishLoadingImageForElement(HTMLImageElement& element)
{
    element.protectedDocument()->checkedEventLoop()->queueTask(TaskSource::Networking, [element = Ref { element }]() {
        RefPtr frame = element->document().frame();
        if (!frame)
            return;

        frame->protectedEditor()->revealSelectionIfNeededAfterLoadingImageForElement(element);

        if (element->document().frame() != frame)
            return;

        if (RefPtr page = frame->page()) {
#if ENABLE(IMAGE_ANALYSIS)
            if (RefPtr queue = page->imageAnalysisQueueIfExists())
                queue->enqueueIfNeeded(element);
#endif
            page->chrome().client().didFinishLoadingImageForElement(element);
        }
    });
}

void Page::didFinishLoadingImageForSVGImage(SVGImageElement& element)
{
    chrome().client().didFinishLoadingImageForSVGImage(element);
}

#if ENABLE(TEXT_AUTOSIZING)

void Page::recomputeTextAutoSizingInAllFrames()
{
    ASSERT(settings().textAutosizingEnabled() && settings().textAutosizingUsesIdempotentMode());
    forEachDocument([] (Document& document) {
        if (CheckedPtr renderView = document.renderView()) {
            for (auto& renderer : descendantsOfType<RenderElement>(*renderView)) {
                // Use the fact that descendantsOfType() returns parent nodes before child nodes.
                // The adjustment is only valid if the parent nodes have already been updated.
                if (RefPtr element = renderer.element()) {
                    if (auto adjustment = Style::Adjuster::adjustmentForTextAutosizing(renderer.style(), *element)) {
                        auto newStyle = RenderStyle::clone(renderer.style());
                        Style::Adjuster::adjustForTextAutosizing(newStyle, adjustment);
                        renderer.setStyle(WTFMove(newStyle));
                    }
                }
            }
        }
    });
}

#endif

OptionSet<FilterRenderingMode> Page::preferredFilterRenderingModes() const
{
    OptionSet<FilterRenderingMode> modes = FilterRenderingMode::Software;
#if USE(CORE_IMAGE)
    if (settings().acceleratedFiltersEnabled())
        modes.add(FilterRenderingMode::Accelerated);
#endif
#if USE(SKIA)
    if (settings().acceleratedCompositingEnabled())
        modes.add(FilterRenderingMode::Accelerated);
#endif
#if USE(GRAPHICS_CONTEXT_FILTERS)
    if (settings().graphicsContextFiltersEnabled())
        modes.add(FilterRenderingMode::GraphicsContext);
#endif
    return modes;
}

bool Page::shouldDisableCorsForRequestTo(const URL& url) const
{
    return std::ranges::any_of(m_corsDisablingPatterns, [&](auto& pattern) {
        return pattern.matches(url);
    });
}

const URL Page::fragmentDirectiveURLForSelectedText()
{
    RefPtr focusedOrMainFrame = focusController().focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return { };

    if (auto range = focusedOrMainFrame->selection().selection().range()) {
        FragmentDirectiveGenerator fragmentDirectiveGenerator(range.value());
        return fragmentDirectiveGenerator.urlWithFragment();
    }
    return { };
}

void Page::revealCurrentSelection()
{
    RefPtr focusedOrMainFrame = focusController().focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return;
    focusedOrMainFrame->checkedSelection()->revealSelection(SelectionRevealMode::Reveal, ScrollAlignment::alignCenterIfNeeded);
}

void Page::injectUserStyleSheet(UserStyleSheet& userStyleSheet)
{
#if ENABLE(APP_BOUND_DOMAINS)
    if (RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get())) {
        if (localMainFrame->loader().client().shouldEnableInAppBrowserPrivacyProtections()) {
            if (RefPtr document = localMainFrame->document())
                document->addConsoleMessage(MessageSource::Security, MessageLevel::Warning, "Ignoring user style sheet for non-app bound domain."_s);
            return;
        }
        localMainFrame->loader().client().notifyPageOfAppBoundBehavior();
    }
#endif

    // We need to wait until we're no longer displaying the initial empty document before we can inject the stylesheets.
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (localMainFrame && localMainFrame->loader().stateMachine().isDisplayingInitialEmptyDocument()) {
        m_userStyleSheetsPendingInjection.append(userStyleSheet);
        return;
    }

    if (userStyleSheet.injectedFrames() == UserContentInjectedFrames::InjectInTopFrameOnly) {
        if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr)
            document->checkedExtensionStyleSheets()->injectPageSpecificUserStyleSheet(userStyleSheet);
    } else {
        forEachDocument([&] (Document& document) {
            document.checkedExtensionStyleSheets()->injectPageSpecificUserStyleSheet(userStyleSheet);
        });
    }
}

void Page::removeInjectedUserStyleSheet(UserStyleSheet& userStyleSheet)
{
    if (!m_userStyleSheetsPendingInjection.isEmpty()) {
        m_userStyleSheetsPendingInjection.removeFirstMatching([userStyleSheet](auto& storedUserStyleSheet) {
            return storedUserStyleSheet.url() == userStyleSheet.url();
        });
        return;
    }

    if (userStyleSheet.injectedFrames() == UserContentInjectedFrames::InjectInTopFrameOnly) {
        RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
        if (RefPtr document = localMainFrame ? localMainFrame->document() : nullptr)
            document->checkedExtensionStyleSheets()->removePageSpecificUserStyleSheet(userStyleSheet);
    } else {
        forEachDocument([&] (Document& document) {
            document.checkedExtensionStyleSheets()->removePageSpecificUserStyleSheet(userStyleSheet);
        });
    }
}

void Page::mainFrameDidChangeToNonInitialEmptyDocument()
{
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    ASSERT_UNUSED(localMainFrame, !localMainFrame || !localMainFrame->loader().stateMachine().isDisplayingInitialEmptyDocument());
    for (auto& userStyleSheet : m_userStyleSheetsPendingInjection)
        injectUserStyleSheet(userStyleSheet);
    m_userStyleSheetsPendingInjection.clear();
}

SpeechRecognitionConnection& Page::speechRecognitionConnection()
{
    return m_speechRecognitionProvider->speechRecognitionConnection();
}

WTF::TextStream& operator<<(WTF::TextStream& ts, RenderingUpdateStep step)
{
    switch (step) {
    case RenderingUpdateStep::Reveal: ts << "Reveal"_s; break;
    case RenderingUpdateStep::FlushAutofocusCandidates: ts << "FlushAutofocusCandidates"_s; break;
    case RenderingUpdateStep::Resize: ts << "Resize"_s; break;
    case RenderingUpdateStep::Scroll: ts << "Scroll"_s; break;
    case RenderingUpdateStep::MediaQueryEvaluation: ts << "MediaQueryEvaluation"_s; break;
    case RenderingUpdateStep::Animations: ts << "Animations"_s; break;
    case RenderingUpdateStep::Fullscreen: ts << "Fullscreen"_s; break;
    case RenderingUpdateStep::AnimationFrameCallbacks: ts << "AnimationFrameCallbacks"_s; break;
    case RenderingUpdateStep::PerformPendingViewTransitions: ts << "PerformPendingViewTransitions"_s; break;
    case RenderingUpdateStep::IntersectionObservations: ts << "IntersectionObservations"_s; break;
    case RenderingUpdateStep::UpdateContentRelevancy: ts << "UpdateContentRelevancy"_s; break;
    case RenderingUpdateStep::ResizeObservations: ts << "ResizeObservations"_s; break;
    case RenderingUpdateStep::Images: ts << "Images"_s; break;
    case RenderingUpdateStep::WheelEventMonitorCallbacks: ts << "WheelEventMonitorCallbacks"_s; break;
    case RenderingUpdateStep::CursorUpdate: ts << "CursorUpdate"_s; break;
    case RenderingUpdateStep::EventRegionUpdate: ts << "EventRegionUpdate"_s; break;
    case RenderingUpdateStep::LayerFlush: ts << "LayerFlush"_s; break;
#if ENABLE(ASYNC_SCROLLING)
    case RenderingUpdateStep::ScrollingTreeUpdate: ts << "ScrollingTreeUpdate"_s; break;
#endif
    case RenderingUpdateStep::VideoFrameCallbacks: ts << "VideoFrameCallbacks"_s; break;
    case RenderingUpdateStep::PrepareCanvasesForDisplayOrFlush: ts << "PrepareCanvasesForDisplayOrFlush"_s; break;
    case RenderingUpdateStep::CaretAnimation: ts << "CaretAnimation"_s; break;
    case RenderingUpdateStep::FocusFixup: ts << "FocusFixup"_s; break;
    case RenderingUpdateStep::UpdateValidationMessagePositions: ts << "UpdateValidationMessagePositions"_s; break;
#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
    case RenderingUpdateStep::AccessibilityRegionUpdate: ts << "AccessibilityRegionUpdate"_s; break;
#endif
    case RenderingUpdateStep::RestoreScrollPositionAndViewState: ts << "RestoreScrollPositionAndViewState"_s; break;
    case RenderingUpdateStep::AdjustVisibility: ts << "AdjustVisibility"_s; break;
    case RenderingUpdateStep::SnapshottedScrollOffsets: ts << "SnapshottedScrollOffsets"_s; break;
    }
    return ts;
}

ImageOverlayController& Page::imageOverlayController()
{
    if (!m_imageOverlayController)
        m_imageOverlayController = makeUnique<ImageOverlayController>(*this);
    return *m_imageOverlayController;
}

Page* Page::serviceWorkerPage(ScriptExecutionContextIdentifier serviceWorkerPageIdentifier)
{
    RefPtr serviceWorkerPageDocument = Document::allDocumentsMap().get(serviceWorkerPageIdentifier);
    return serviceWorkerPageDocument ? serviceWorkerPageDocument->page() : nullptr;
}

#if ENABLE(IMAGE_ANALYSIS)

ImageAnalysisQueue& Page::imageAnalysisQueue()
{
    if (!m_imageAnalysisQueue)
        m_imageAnalysisQueue = ImageAnalysisQueue::create(*this);
    return *m_imageAnalysisQueue;
}

Ref<ImageAnalysisQueue> Page::protectedImageAnalysisQueue()
{
    return imageAnalysisQueue();
}

void Page::resetImageAnalysisQueue()
{
    if (RefPtr previousQueue = std::exchange(m_imageAnalysisQueue, { }))
        previousQueue->clear();
}

void Page::updateElementsWithTextRecognitionResults()
{
    if (m_textRecognitionResults.isEmptyIgnoringNullReferences())
        return;

    m_textRecognitionResults.removeNullReferences();

    Vector<std::pair<Ref<HTMLElement>, TextRecognitionResult>> elementsToUpdate;
    for (auto entry : m_textRecognitionResults) {
        Ref protectedElement = entry.key;
        if (!protectedElement->isConnected())
            continue;

        auto renderer = protectedElement->renderer();
        if (!is<RenderImage>(renderer))
            continue;

        auto& [result, containerRect] = entry.value;
        auto newContainerRect = ImageOverlay::containerRect(protectedElement.get());
        if (containerRect == newContainerRect)
            continue;

        containerRect = newContainerRect;
        elementsToUpdate.append({ WTFMove(protectedElement), result });
    }

    for (auto& [element, result] : elementsToUpdate) {
        element->protectedDocument()->checkedEventLoop()->queueTask(TaskSource::InternalAsyncTask, [result = TextRecognitionResult { result }, weakElement = WeakPtr { element }] {
            if (RefPtr element = weakElement.get())
                ImageOverlay::updateWithTextRecognitionResult(*element, result, ImageOverlay::CacheTextRecognitionResults::No);
        });
    }
}

bool Page::hasCachedTextRecognitionResult(const HTMLElement& element) const
{
    return m_textRecognitionResults.contains(element);
}

std::optional<TextRecognitionResult> Page::cachedTextRecognitionResult(const HTMLElement& element) const
{
    auto iterator = m_textRecognitionResults.find(element);
    if (iterator == m_textRecognitionResults.end())
        return std::nullopt;

    return { iterator->value.first };
}

void Page::cacheTextRecognitionResult(const HTMLElement& element, const IntRect& containerRect, const TextRecognitionResult& result)
{
    m_textRecognitionResults.set(element, CachedTextRecognitionResult { result, containerRect });
}

void Page::resetTextRecognitionResults()
{
    m_textRecognitionResults.clear();
}

void Page::resetTextRecognitionResult(const HTMLElement& element)
{
    m_textRecognitionResults.remove(element);
}

#endif // ENABLE(IMAGE_ANALYSIS)

JSC::JSGlobalObject* Page::serviceWorkerGlobalObject(DOMWrapperWorld& world)
{
    RefPtr serviceWorkerGlobalScope = m_serviceWorkerGlobalScope.get();
    if (!serviceWorkerGlobalScope)
        return nullptr;

    CheckedPtr scriptController = serviceWorkerGlobalScope->script();
    if (!scriptController)
        return nullptr;

    // FIXME: We currently do not support non-normal worlds in service workers.
    RELEASE_ASSERT(&downcast<JSVMClientData>(serviceWorkerGlobalScope->vm().clientData)->normalWorldSingleton() == &world);
    return scriptController->globalScopeWrapper();
}

void Page::setServiceWorkerGlobalScope(ServiceWorkerGlobalScope& serviceWorkerGlobalScope)
{
    ASSERT(isMainThread());
    ASSERT(m_isServiceWorkerPage);
    m_serviceWorkerGlobalScope = serviceWorkerGlobalScope;
}

StorageConnection& Page::storageConnection()
{
    return m_storageProvider->storageConnection();
}

ModelPlayerProvider& Page::modelPlayerProvider()
{
    return m_modelPlayerProvider.get();
}

void Page::setupForRemoteWorker(const URL& scriptURL, const SecurityOriginData& topOrigin, const String& referrerPolicy, OptionSet<AdvancedPrivacyProtections> advancedPrivacyProtections)
{
    RefPtr localMainFrame = this->localMainFrame();
    if (!localMainFrame)
        return;
    // FIXME: <rdar://117922051> Investigate if the correct origins are set here with site isolation enabled.
    localMainFrame->loader().initForSynthesizedDocument({ });
    Ref document = Document::createNonRenderedPlaceholder(*localMainFrame, scriptURL);
    document->createDOMWindow();
    document->storageBlockingStateDidChange();

    Ref origin = topOrigin.securityOrigin();
    URL originAsURL = origin->toURL();
    document->setSiteForCookies(originAsURL);
    document->setFirstPartyForCookies(originAsURL);

    if (RefPtr documentLoader = localMainFrame->loader().documentLoader())
        documentLoader->setAdvancedPrivacyProtections(advancedPrivacyProtections);

    if (document->settings().storageBlockingPolicy() != StorageBlockingPolicy::BlockThirdParty)
        document->setDomainForCachePartition(String { emptyString() });
    else
        document->setDomainForCachePartition(origin->domainForCachePartition());

    if (auto policy = parseReferrerPolicy(referrerPolicy, ReferrerPolicySource::HTTPHeader))
        document->setReferrerPolicy(*policy);

    localMainFrame->setDocument(WTFMove(document));
}

void Page::forceRepaintAllFrames()
{
    for (RefPtr frame = mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame)
            continue;
        RefPtr frameView = localFrame->view();
        if (!frameView || !frameView->renderView())
            continue;

        frameView->checkedRenderView()->repaintViewAndCompositedLayers();
    }
}

#if ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
void Page::updatePlayStateForAllAnimations()
{
    RefPtr localMainFrame = this->localMainFrame();
    if (RefPtr view = localMainFrame ? localMainFrame->view() : nullptr)
        view->updatePlayStateForAllAnimationsIncludingSubframes();
}

void Page::addIndividuallyPlayingAnimationElement(HTMLImageElement& element)
{
    ASSERT(element.allowsAnimation());
    bool wasEmpty = !m_individuallyPlayingAnimationElements.computeSize();
    m_individuallyPlayingAnimationElements.add(element);

    // If there were no individually playing animations prior to this addition, then the effective state of isAnyAnimationAllowedToPlay has changed.
    if (wasEmpty && !m_imageAnimationEnabled)
        chrome().client().isAnyAnimationAllowedToPlayDidChange(true);
}

void Page::removeIndividuallyPlayingAnimationElement(HTMLImageElement& element)
{
    m_individuallyPlayingAnimationElements.remove(element);

    // If removing this animation caused there to be no remaining individually playing animations,
    // then the effective state of isAnyAnimationAllowedToPlay has changed.
    if (!m_individuallyPlayingAnimationElements.computeSize() && !m_imageAnimationEnabled)
        chrome().client().isAnyAnimationAllowedToPlayDidChange(false);
}
#endif // ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)

ScreenOrientationManager* Page::screenOrientationManager() const
{
    return m_screenOrientationManager.get();
}

URL Page::applyLinkDecorationFiltering(const URL& url, LinkDecorationFilteringTrigger trigger) const
{
    return chrome().client().applyLinkDecorationFiltering(url, trigger);
}

String Page::applyLinkDecorationFiltering(const String& urlString, LinkDecorationFilteringTrigger trigger) const
{
    if (auto url = URL { urlString }; url.isValid()) {
        if (auto sanitizedURL = applyLinkDecorationFiltering(WTFMove(url), trigger); sanitizedURL != url)
            return sanitizedURL.string();
    }
    return urlString;
}

URL Page::allowedQueryParametersForAdvancedPrivacyProtections(const URL& url) const
{
    return chrome().client().allowedQueryParametersForAdvancedPrivacyProtections(url);
}

void Page::willBeginScrolling()
{
}

void Page::didFinishScrolling()
{
}

void Page::addRootFrame(LocalFrame& frame)
{
    ASSERT(frame.isRootFrame());
    ASSERT(!m_rootFrames.contains(frame));
    m_rootFrames.add(frame);
    chrome().client().rootFrameAdded(frame);
}

void Page::removeRootFrame(LocalFrame& frame)
{
    ASSERT(frame.isRootFrame());
    ASSERT(m_rootFrames.contains(frame));
    m_rootFrames.remove(frame);
    chrome().client().rootFrameRemoved(frame);
}

String Page::ensureMediaKeysStorageDirectoryForOrigin(const SecurityOriginData& origin)
{
    if (usesEphemeralSession())
        return emptyString();

    return m_storageProvider->ensureMediaKeysStorageDirectoryForOrigin(origin);
}

void Page::setMediaKeysStorageDirectory(const String& directory)
{
    m_storageProvider->setMediaKeysStorageDirectory(directory);
}

void Page::reloadExecutionContextsForOrigin(const ClientOrigin& origin, std::optional<FrameIdentifier> triggeringFrame) const
{
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(m_mainFrame.get());
    if (!localMainFrame || localMainFrame->protectedDocument()->topOrigin().data() != origin.topOrigin)
        return;

    for (RefPtr frame = m_mainFrame.get(); frame;) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        if (!localFrame || frame->frameID() == triggeringFrame) {
            frame = frame->tree().traverseNext();
            continue;
        }
        RefPtr document = localFrame->document();
        if (!document || document->securityOrigin().data() != origin.clientOrigin) {
            frame = frame->tree().traverseNext();
            continue;
        }
        localFrame->protectedNavigationScheduler()->scheduleRefresh(*document);
        frame = frame->tree().traverseNextSkippingChildren();
    }
}

void Page::opportunisticallyRunIdleCallbacks(MonotonicTime deadline)
{
    forEachWindowEventLoop([&](auto& eventLoop) {
        eventLoop.opportunisticallyRunIdleCallbacks(deadline);
    });
}

void Page::willChangeLocationInCompletelyLoadedSubframe()
{
    commonVM().heap.scheduleOpportunisticFullCollection();
}

void Page::performOpportunisticallyScheduledTasks(MonotonicTime deadline)
{
    OptionSet<JSC::VM::SchedulerOptions> options;
    if (m_opportunisticTaskScheduler->hasImminentlyScheduledWork())
        options.add(JSC::VM::SchedulerOptions::HasImminentlyScheduledWork);
    commonVM().performOpportunisticallyScheduledTasks(deadline, options);

    deleteRemovedNodesAndDetachedRenderers();
}

void Page::deleteRemovedNodesAndDetachedRenderers()
{
    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(mainFrame());
    if (!localMainFrame)
        return;
    RefPtr document = localMainFrame->document();
    if (!document)
        return;
    forEachLocalFrame([] (LocalFrame& frame) {
        RefPtr document = frame.document();
        if (!document)
            return;
        document->asyncNodeDeletionQueue().deleteNodesNow();
        RefPtr frameView = document->view();
        if (!frameView)
            return;
        frameView->layoutContext().deleteDetachedRenderersNow();
    });
}

CheckedRef<ProgressTracker> Page::checkedProgress()
{
    return m_progress.get();
}

CheckedRef<const ProgressTracker> Page::checkedProgress() const
{
    return m_progress.get();
}

CheckedRef<ElementTargetingController> Page::checkedElementTargetingController()
{
    return m_elementTargetingController.get();
}

const String& Page::sceneIdentifier() const
{
#if PLATFORM(IOS_FAMILY)
    return m_sceneIdentifier;
#else
    return emptyString();
#endif
}

#if PLATFORM(IOS_FAMILY)
void Page::setSceneIdentifier(String&& sceneIdentifier)
{
    if (m_sceneIdentifier == sceneIdentifier)
        return;
    m_sceneIdentifier = WTFMove(sceneIdentifier);

    forEachDocument([&] (Document& document) {
        document.sceneIdentifierDidChange();
    });
}

void Page::setObscuredInsets(const FloatBoxExtent& insets)
{
    RefPtr localMainFrame = this->localMainFrame();
    RefPtr view = localMainFrame ? localMainFrame->view() : nullptr;

    if (m_obscuredInsets == insets) {
        if (view)
            view->clearObscuredInsetsAdjustmentsIfNeeded();
        return;
    }

    if (view)
        view->obscuredInsetsWillChange(insets - m_obscuredInsets);
    m_obscuredInsets = insets;
    m_chrome->client().setNeedsFixedContainerEdgesUpdate();
}
#endif

void Page::updateFixedContainerEdges(BoxSideSet sides)
{
    RefPtr mainFrame = localMainFrame();
    if (!mainFrame)
        return;

    RefPtr document = mainFrame->document();
    if (!document)
        return;

    RefPtr frameView = mainFrame->view();
    if (!frameView)
        return;

    auto [edges, elements] = frameView->fixedContainerEdges([&] {
        auto sidesToSample = sides;
        auto scrollOffset = frameView->scrollOffset();
        auto minimumOffset = frameView->minimumScrollOffset();
        auto maximumOffset = frameView->maximumScrollOffset();

        bool canSampleTopEdge = settings().topContentInsetBackgroundCanChangeAfterScrolling()
            || !frameView->wasEverScrolledExplicitlyByUser()
            || document->parsing();

        if (scrollOffset.y() < minimumOffset.y() || !canSampleTopEdge)
            sidesToSample.remove(BoxSideFlag::Top);

        if (scrollOffset.y() > maximumOffset.y())
            sidesToSample.remove(BoxSideFlag::Bottom);

        if (scrollOffset.x() < minimumOffset.x())
            sidesToSample.remove(BoxSideFlag::Left);

        if (scrollOffset.x() > maximumOffset.x())
            sidesToSample.remove(BoxSideFlag::Right);

        return sidesToSample;
    }());

    for (auto sideFlag : sides) {
        auto side = boxSideFromFlag(sideFlag);
        if (!edges.hasFixedEdge(side) || (!edges.predominantColor(side).isVisible() && fixedContainerEdges().predominantColor(side).isVisible())) {
            WeakPtr lastElement = m_fixedContainerEdgesAndElements.second.at(side);
            if (!lastElement)
                continue;

            CheckedPtr renderer = lastElement->renderer();
            if (!renderer)
                continue;

            if (renderer->style().usedVisibility() != Visibility::Visible
                && (side != BoxSide::Top || !lastElement->hasTagName(HTMLNames::headerTag))
                && (side != BoxSide::Bottom || !lastElement->hasTagName(HTMLNames::footerTag)))
                continue;

            elements.setAt(side, WTFMove(lastElement));
            edges.colors.setAt(side, fixedContainerEdges().colors.at(side));
        }
    }

    m_fixedContainerEdgesAndElements = std::make_pair(makeUniqueRef<FixedContainerEdges>(WTFMove(edges)), WTFMove(elements));

#if HAVE(RUBBER_BANDING)
    auto topOverhangColor = fixedContainerEdges().predominantColor(BoxSide::Top);
    if (RefPtr layer = frameView->setWantsLayerForTopOverhangColorExtension(topOverhangColor.isVisible())) {
        layer->setBackgroundColor(WTFMove(topOverhangColor));
        if (CheckedPtr renderView = frameView->renderView())
            renderView->compositor().updateSizeAndPositionForTopOverhangColorExtensionLayer();
    }
#endif
}

Element* Page::lastFixedContainer(BoxSide side) const
{
    return m_fixedContainerEdgesAndElements.second.at(side).get();
}

void Page::setPortsForUpgradingInsecureSchemeForTesting(uint16_t upgradeFromInsecurePort, uint16_t upgradeToSecurePort)
{
    m_portsForUpgradingInsecureSchemeForTesting = { upgradeFromInsecurePort, upgradeToSecurePort };
}

std::optional<std::pair<uint16_t, uint16_t>> Page::portsForUpgradingInsecureSchemeForTesting() const
{
    return m_portsForUpgradingInsecureSchemeForTesting;
}

#if USE(ATSPI)
AccessibilityRootAtspi* Page::accessibilityRootObject() const
{
    return m_accessibilityRootObject.get();
}

void Page::setAccessibilityRootObject(AccessibilityRootAtspi* rootObject)
{
    m_accessibilityRootObject = rootObject;
}
#endif // USE(ATSPI)

#if ENABLE(WEBXR)
#if PLATFORM(IOS_FAMILY)
bool Page::hasActiveImmersiveSession() const
{
    return !!activeImmersiveXRSession();
}
#endif // PLATFORM(IOS_FAMILY)

RefPtr<WebXRSession> Page::activeImmersiveXRSession() const
{
    for (RefPtr frame = m_mainFrame.get(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
        RefPtr window = localFrame ? localFrame->window() : nullptr;
        if (!window)
            continue;

        RefPtr navigator = window->optionalNavigator();
        if (!navigator)
            continue;

        if (auto xrSystem = NavigatorWebXR::xrIfExists(*navigator))
            return xrSystem->activeImmersiveSession();
    }

    return nullptr;
}
#endif //  ENABLE(WEBXR)

#if HAVE(SPATIAL_TRACKING_LABEL)
void Page::setDefaultSpatialTrackingLabel(const String& label)
{
    if (m_defaultSpatialTrackingLabel == label)
        return;
    m_defaultSpatialTrackingLabel = WTFMove(label);

    forEachDocument([&] (Document& document) {
        document.defaultSpatialTrackingLabelChanged(m_defaultSpatialTrackingLabel);
    });
}
#endif

#if ENABLE(GAMEPAD)
void Page::gamepadsRecentlyAccessed()
{
    if (MonotonicTime::now() - m_lastAccessNotificationTime < NavigatorGamepad::gamepadsRecentlyAccessedThreshold())
        return;

    chrome().client().gamepadsRecentlyAccessed();
    m_lastAccessNotificationTime = MonotonicTime::now();
}

#if PLATFORM(VISION)
void Page::allowGamepadAccess()
{
    if (m_gamepadAccessGranted)
        return;

    m_gamepadAccessGranted = true;
    GamepadManager::singleton().updateQuarantineStatus();
}

void Page::initializeGamepadAccessForPageLoad()
{
    m_gamepadAccessGranted = m_gamepadAccessRequiresExplicitConsent == ShouldRequireExplicitConsentForGamepadAccess::No;
}
#endif // PLATFORM(VISION)

#endif // ENABLE(GAMEPAD)

#if ENABLE(WRITING_TOOLS)
void Page::willBeginWritingToolsSession(const std::optional<WritingTools::Session>& session, CompletionHandler<void(const Vector<WritingTools::Context>&)>&& completionHandler)
{
    m_writingToolsController->willBeginWritingToolsSession(session, WTFMove(completionHandler));
}

void Page::didBeginWritingToolsSession(const WritingTools::Session& session, const Vector<WritingTools::Context>& contexts)
{
    m_writingToolsController->didBeginWritingToolsSession(session, contexts);
}

void Page::proofreadingSessionDidReceiveSuggestions(const WritingTools::Session& session, const Vector<WritingTools::TextSuggestion>& suggestions, const CharacterRange& processedRange, const WritingTools::Context& context, bool finished)
{
    m_writingToolsController->proofreadingSessionDidReceiveSuggestions(session, suggestions, processedRange, context, finished);
}

void Page::proofreadingSessionDidUpdateStateForSuggestion(const WritingTools::Session& session, WritingTools::TextSuggestion::State state, const WritingTools::TextSuggestion& suggestion, const WritingTools::Context& context)
{
    m_writingToolsController->proofreadingSessionDidUpdateStateForSuggestion(session, state, suggestion, context);
}

void Page::willEndWritingToolsSession(const WritingTools::Session& session, bool accepted)
{
    m_writingToolsController->willEndWritingToolsSession(session, accepted);
}

void Page::didEndWritingToolsSession(const WritingTools::Session& session, bool accepted)
{
    m_writingToolsController->didEndWritingToolsSession(session, accepted);
}

void Page::compositionSessionDidReceiveTextWithReplacementRange(const WritingTools::Session& session, const AttributedString& attributedText, const CharacterRange& range, const WritingTools::Context& context, bool finished)
{
    m_writingToolsController->compositionSessionDidReceiveTextWithReplacementRange(session, attributedText, range, context, finished);
}

void Page::writingToolsSessionDidReceiveAction(const WritingTools::Session& session, WritingTools::Action action)
{
    m_writingToolsController->writingToolsSessionDidReceiveAction(session, action);
}

void Page::updateStateForSelectedSuggestionIfNeeded()
{
    m_writingToolsController->updateStateForSelectedSuggestionIfNeeded();
}

void Page::respondToUnappliedWritingToolsEditing(EditCommandComposition* command)
{
    m_writingToolsController->respondToUnappliedEditing(command);
}

void Page::respondToReappliedWritingToolsEditing(EditCommandComposition* command)
{
    m_writingToolsController->respondToReappliedEditing(command);
}

Vector<FloatRect> Page::proofreadingSessionSuggestionTextRectsInRootViewCoordinates(const CharacterRange& enclosingRangeRelativeToSessionRange) const
{
    RefPtr localTopDocument = this->localTopDocument();
    if (!localTopDocument) {
        ASSERT_NOT_REACHED();
        return { };
    }

    auto scope = m_writingToolsController->activeSessionRange();
    if (!scope) {
        ASSERT_NOT_REACHED();
        return { };
    }

    return IntelligenceTextEffectsSupport::writingToolsTextSuggestionRectsInRootViewCoordinates(*localTopDocument, *scope, enclosingRangeRelativeToSessionRange);
}

void Page::updateTextVisibilityForActiveWritingToolsSession(const CharacterRange& rangeRelativeToSessionRange, bool visible, const WTF::UUID& identifier)
{
    RefPtr localTopDocument = this->localTopDocument();
    if (!localTopDocument) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto scope = m_writingToolsController->activeSessionRange();
    if (!scope) {
        ASSERT_NOT_REACHED();
        return;
    }

    IntelligenceTextEffectsSupport::updateTextVisibility(*localTopDocument, *scope, rangeRelativeToSessionRange, visible, identifier);
}

RefPtr<TextIndicator> Page::textPreviewDataForActiveWritingToolsSession(const CharacterRange& rangeRelativeToSessionRange)
{
    RefPtr localTopDocument = this->localTopDocument();
    if (!localTopDocument) {
        ASSERT_NOT_REACHED();
        return nullptr;
    }

    auto scope = m_writingToolsController->activeSessionRange();
    if (!scope) {
        ASSERT_NOT_REACHED();
        return nullptr;
    }

    return IntelligenceTextEffectsSupport::textPreviewDataForRange(*localTopDocument, *scope, rangeRelativeToSessionRange);
}

void Page::decorateTextReplacementsForActiveWritingToolsSession(const CharacterRange& rangeRelativeToSessionRange)
{
    RefPtr localTopDocument = this->localTopDocument();
    if (!localTopDocument) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto scope = m_writingToolsController->activeSessionRange();
    if (!scope) {
        ASSERT_NOT_REACHED();
        return;
    }

    IntelligenceTextEffectsSupport::decorateWritingToolsTextReplacements(*localTopDocument, *scope, rangeRelativeToSessionRange);
}

void Page::setSelectionForActiveWritingToolsSession(const CharacterRange& rangeRelativeToSessionRange)
{
    RefPtr localTopDocument = this->localTopDocument();
    if (!localTopDocument) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto scope = m_writingToolsController->activeSessionRange();
    if (!scope) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto resolvedRange = resolveCharacterRange(*scope, rangeRelativeToSessionRange);
    auto visibleSelection = VisibleSelection { resolvedRange };
    if (visibleSelection.isNoneOrOrphaned())
        return;

    localTopDocument->selection().setSelection(visibleSelection);
}

std::optional<SimpleRange> Page::contextRangeForActiveWritingToolsSession() const
{
    return m_writingToolsController->activeSessionRange();
}

void Page::intelligenceTextAnimationsDidComplete()
{
    m_writingToolsController->intelligenceTextAnimationsDidComplete();
}
#endif

void Page::hasActiveNowPlayingSessionChanged()
{
    if (!m_activeNowPlayingSessionUpdateTimer.isActive())
        m_activeNowPlayingSessionUpdateTimer.startOneShot(0_s);
}

void Page::updateActiveNowPlayingSessionNow()
{
    if (m_activeNowPlayingSessionUpdateTimer.isActive())
        m_activeNowPlayingSessionUpdateTimer.stop();

    if (!mediaSessionManagerIfExists())
        return;

    bool hasActiveNowPlayingSession = mediaSessionManager().hasActiveNowPlayingSessionInGroup(mediaSessionGroupIdentifier());
    if (hasActiveNowPlayingSession == m_hasActiveNowPlayingSession)
        return;

    m_hasActiveNowPlayingSession = hasActiveNowPlayingSession;
    chrome().client().hasActiveNowPlayingSessionChanged(hasActiveNowPlayingSession);
}

void Page::setLastAuthentication(LoginStatus::AuthenticationType authType)
{
    auto loginStatus = LoginStatus::create(RegistrableDomain(mainFrameURL()), emptyString(), LoginStatus::CredentialTokenType::HTTPStateToken, authType, LoginStatus::TimeToLiveAuthentication);
    if (loginStatus.hasException())
        return;
    m_lastAuthentication = loginStatus.releaseReturnValue().moveToUniquePtr();

    if (RefPtr document = localMainFrame() ? localMainFrame()->document() : nullptr)
        ResourceLoadObserver::shared().logUserInteractionWithReducedTimeResolution(*document);
}

#if ENABLE(FULLSCREEN_API)
bool Page::isDocumentFullscreenEnabled() const
{
    return m_settings->fullScreenEnabled() || m_settings->videoFullscreenRequiresElementFullscreen();
}
#endif

void Page::startDeferringResizeEvents()
{
    m_shouldDeferResizeEvents = true;
}

void Page::flushDeferredResizeEvents()
{
    m_shouldDeferResizeEvents = false;
    forEachDocument([&] (Document& document) {
        document.flushDeferredResizeEvents();
    });
}

void Page::startDeferringScrollEvents()
{
    m_shouldDeferScrollEvents = true;
}

void Page::flushDeferredScrollEvents()
{
    m_shouldDeferScrollEvents = false;
    forEachDocument([&] (Document& document) {
        document.flushDeferredScrollEvents();
    });
}

bool Page::reportScriptTrackingPrivacy(const URL& url, ScriptTrackingPrivacyCategory category)
{
    return !url.isEmpty() && m_scriptTrackingPrivacyReports.add({ url, category }).isNewEntry;
}

bool Page::shouldAllowScriptAccess(const URL& url, const SecurityOrigin& topOrigin, ScriptTrackingPrivacyCategory category) const
{
    return chrome().client().shouldAllowScriptAccess(url, topOrigin, category);
}

bool Page::requiresScriptTrackingPrivacyProtections(const URL& scriptURL) const
{
    if (!advancedPrivacyProtections().contains(AdvancedPrivacyProtections::ScriptTrackingPrivacy))
        return false;

    return chrome().client().requiresScriptTrackingPrivacyProtections(scriptURL, mainFrameOrigin());
}

void Page::applyWindowFeatures(const WindowFeatures& features)
{
    Ref frame = mainFrame();
    chrome().setToolbarsVisible(features.toolBarVisible || features.locationBarVisible);

    if (!frame->page())
        return;
    if (features.statusBarVisible)
        chrome().setStatusbarVisible(*features.statusBarVisible);

    if (!frame->page())
        return;
    if (features.scrollbarsVisible)
        chrome().setScrollbarsVisible(*features.scrollbarsVisible);

    if (!frame->page())
        return;
    if (features.menuBarVisible)
        chrome().setMenubarVisible(*features.menuBarVisible);

    if (!frame->page())
        return;
    if (features.resizable)
        chrome().setResizable(*features.resizable);

    // 'x' and 'y' specify the location of the window, while 'width' and 'height'
    // specify the size of the viewport. We can only resize the window, so adjust
    // for the difference between the window size and the viewport size.

    // FIXME: We should reconcile the initialization of viewport arguments between iOS and non-IOS.
#if !PLATFORM(IOS_FAMILY)
    FloatSize viewportSize = chrome().pageRect().size();
    FloatRect windowRect = chrome().windowRect();
    if (features.x)
        windowRect.setX(*features.x);
    if (features.y)
        windowRect.setY(*features.y);
    // Zero width and height mean using default size, not minimum one.
    if (features.width && *features.width)
        windowRect.setWidth(*features.width + (windowRect.width() - viewportSize.width()));
    if (features.height && *features.height)
        windowRect.setHeight(*features.height + (windowRect.height() - viewportSize.height()));

#if PLATFORM(GTK)
    // Use the size of the previous window if there is no default size.
    if (!windowRect.width())
        windowRect.setWidth(features.oldWindowRect.width());
    if (!windowRect.height())
        windowRect.setHeight(features.oldWindowRect.height());
#endif

    // Ensure non-NaN values, minimum size as well as being within valid screen area.
    FloatRect newWindowRect = LocalDOMWindow::adjustWindowRect(*this, windowRect);

    if (!frame->page())
        return;
    chrome().setWindowRect(newWindowRect);
#else
    // On iOS, width and height refer to the viewport dimensions.
    ViewportArguments arguments;
    // Zero width and height mean using default size, not minimum one.
    if (features.width && *features.width)
        arguments.width = *features.width;
    if (features.height && *features.height)
        arguments.height = *features.height;
    if (RefPtr localFrame = dynamicDowncast<LocalFrame>(frame))
        localFrame->setViewportArguments(arguments);
#endif
}

bool Page::isAlwaysOnLoggingAllowed() const
{
    return m_sessionID.isAlwaysOnLoggingAllowed() || settings().allowPrivacySensitiveOperationsInNonPersistentDataStores();
}

Ref<InspectorController> Page::protectedInspectorController()
{
    return m_inspectorController.get();
}

#if PLATFORM(MAC) && (ENABLE(SERVICE_CONTROLS) || ENABLE(TELEPHONE_NUMBER_DETECTION))
Ref<ServicesOverlayController> Page::protectedServicesOverlayController()
{
    return m_servicesOverlayController.get();
}
#endif

ProcessID Page::presentingApplicationPID() const
{
#if HAVE(AUDIT_TOKEN)
    if (m_presentingApplicationAuditToken)
        return pidFromAuditToken(*m_presentingApplicationAuditToken);
#endif

    return WTF::legacyPresentingApplicationPID();
}

#if HAVE(AUDIT_TOKEN)
const std::optional<audit_token_t>& Page::presentingApplicationAuditToken() const
{
    return m_presentingApplicationAuditToken;
}

void Page::setPresentingApplicationAuditToken(std::optional<audit_token_t> presentingApplicationAuditToken)
{
    m_presentingApplicationAuditToken = WTFMove(presentingApplicationAuditToken);

#if ENABLE(EXTENSION_CAPABILITIES)
    if (settings().mediaCapabilityGrantsEnabled())
        return;
#endif

    if (RefPtr mediaSessionManager = mediaSessionManagerIfExists())
        mediaSessionManager->updatePresentingApplicationPIDIfNecessary(presentingApplicationPID());
}
#endif

bool Page::requiresUserGestureForAudioPlayback() const
{
    auto autoplayPolicy = m_mainFrame->autoplayPolicy();
    if (autoplayPolicy != AutoplayPolicy::Default)
        return autoplayPolicy == AutoplayPolicy::AllowWithoutSound || autoplayPolicy == AutoplayPolicy::Deny;
    return m_settings->requiresUserGestureForAudioPlayback();
}

bool Page::requiresUserGestureForVideoPlayback() const
{
    auto autoplayPolicy = m_mainFrame->autoplayPolicy();
    if (autoplayPolicy != AutoplayPolicy::Default)
        return autoplayPolicy == AutoplayPolicy::Deny;
    return m_settings->requiresUserGestureForVideoPlayback();
}

static RefPtr<PlatformMediaSessionManager>& mediaSessionManagerSingleton()
{
    static NeverDestroyed<RefPtr<PlatformMediaSessionManager>> manager;
    return manager.get();
}

MediaSessionManagerInterface& Page::mediaSessionManager()
{
    if (!m_mediaSessionManager) {
        if (!m_mediaSessionManagerFactory) {
            m_mediaSessionManagerFactory = [](std::optional<PageIdentifier> identifier) {
                RefPtr<PlatformMediaSessionManager>& manager = mediaSessionManagerSingleton();
                if (!manager) {
                    manager = PlatformMediaSessionManager::create(identifier);
                    manager->resetRestrictions();
                }
                return manager;
            };
        }

        m_mediaSessionManager = m_mediaSessionManagerFactory.value()(m_identifier);

        MediaEngineConfigurationFactory::setMediaSessionManagerProvider([] (PageIdentifier identifier) {
            return Page::mediaSessionManagerForPageIdentifier(identifier);
        });
    }

    return *m_mediaSessionManager;
}

MediaSessionManagerInterface* Page::mediaSessionManagerIfExists() const
{
    return m_mediaSessionManager.get();
}

MediaSessionManagerInterface* Page::mediaSessionManagerForPageIdentifier(PageIdentifier identifier)
{
    RefPtr<MediaSessionManagerInterface> manager;

    forEachPage([identifier, &manager](auto& page) {
        if (identifier == page.identifier())
            manager = &page.mediaSessionManager();
    });

    return manager.get();
}

#if HAVE(SUPPORT_HDR_DISPLAY)
bool Page::drawsHDRContent() const
{
    bool drawsHDRContent = false;
    forEachRenderableDocument([&] (Document& document) {
        if (document.drawsHDRContent())
            drawsHDRContent = true;
    });
    return drawsHDRContent;
}

void Page::updateDisplayEDRHeadroom()
{
    static constexpr float kMinimumRequiredHeadroomForTonemapping = 2.7;
    bool layersRequireTonemapping = false;
    float headroom = currentEDRHeadroomForDisplay(m_displayID);
    if (m_settings->supportHDRCompositorTonemappingEnabled() && headroom >= kMinimumRequiredHeadroomForTonemapping) {
        headroom = maxEDRHeadroomForDisplay(m_displayID);
        layersRequireTonemapping = true;
    }
    if (headroom == m_displayEDRHeadroom && m_hdrLayersRequireTonemapping == layersRequireTonemapping)
        return;

    LOG_WITH_STREAM(HDR, stream << "Page " << this << " updateDisplayEDRHeadroom " << m_displayEDRHeadroom.headroom << " to " << headroom);
    m_displayEDRHeadroom = Headroom(headroom);
    m_hdrLayersRequireTonemapping = layersRequireTonemapping;

    forEachDocument([&] (Document& document) {
        if (!document.drawsHDRContent())
            return;

        if (RefPtr view = document.view())
            view->setDescendantsNeedUpdateBackingAndHierarchyTraversal();
    });
}

#endif

} // namespace WebCore
