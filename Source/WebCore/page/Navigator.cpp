/*
 *  Copyright (C) 2000 Harri Porten (porten@kde.org)
 *  Copyright (c) 2000 Daniel Molkentin (molkentin@kde.org)
 *  Copyright (c) 2000 Stefan Schimanski (schimmi@kde.org)
 *  Copyright (C) 2003-2025 Apple Inc. All rights reserved.
 *  Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "Navigator.h"

#include "BadgeClient.h"
#include "Chrome.h"
#include "CookieJar.h"
#include "DOMMimeType.h"
#include "DOMMimeTypeArray.h"
#include "DOMPlugin.h"
#include "DOMPluginArray.h"
#include "Document.h"
#include "DocumentInlines.h"
#include "FrameLoader.h"
#include "GPU.h"
#include "Geolocation.h"
#include "JSDOMPromiseDeferred.h"
#include "LoaderStrategy.h"
#include "LocalFrame.h"
#include "LocalFrameLoaderClient.h"
#include "LocalizedStrings.h"
#include "NavigatorUAData.h"
#include "Page.h"
#include "PermissionsPolicy.h"
#include "PlatformStrategies.h"
#include "PluginData.h"
#include "Quirks.h"
#include "ResourceLoadObserver.h"
#include "ScriptController.h"
#include "SecurityOrigin.h"
#include "Settings.h"
#include "ShareData.h"
#include "ShareDataReader.h"
#include "SharedBuffer.h"
#include <JavaScriptCore/ConsoleTypes.h>
#include <wtf/Language.h>
#include <wtf/RunLoop.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(Navigator);

Navigator::Navigator(ScriptExecutionContext* context, LocalDOMWindow& window)
    : NavigatorBase(context)
    , LocalDOMWindowProperty(&window)
{
}

Navigator::~Navigator() = default;

String Navigator::appVersion() const
{
    RefPtr frame = this->frame();
    if (!frame)
        return String();
    if (frame->settings().webAPIStatisticsEnabled())
        ResourceLoadObserver::shared().logNavigatorAPIAccessed(*frame->protectedDocument(), NavigatorAPIsAccessed::AppVersion);
    return NavigatorBase::appVersion();
}

const String& Navigator::userAgent() const
{
    RefPtr frame = this->frame();
    if (!frame || !frame->page())
        return m_userAgent;
    if (frame->settings().webAPIStatisticsEnabled())
        ResourceLoadObserver::shared().logNavigatorAPIAccessed(*frame->protectedDocument(), NavigatorAPIsAccessed::UserAgent);
    if (m_userAgent.isNull())
        m_userAgent = frame->loader().userAgent(frame->document()->url());
    return m_userAgent;
}
    
String Navigator::platform() const
{
    RefPtr frame = this->frame();
    if (!frame || !frame->page())
        return m_platform;

    if (m_platform.isNull())
        m_platform = frame->loader().navigatorPlatform();
    
    if (m_platform.isNull())
        m_platform = NavigatorBase::platform();
    return m_platform;
}

void Navigator::userAgentChanged()
{
    m_userAgent = String();
}

bool Navigator::onLine() const
{
    return platformStrategies()->loaderStrategy()->isOnLine();
}

static std::optional<URL> shareableURLForShareData(ScriptExecutionContext& context, const ShareData& data)
{
    if (data.url.isNull())
        return std::nullopt;

    auto url = context.completeURL(data.url);
    if (!url.isValid())
        return std::nullopt;
    if (!url.protocolIsInHTTPFamily())
        return std::nullopt;

    return url;
}

static bool validateWebSharePolicy(Document& document)
{
    return PermissionsPolicy::isFeatureEnabled(PermissionsPolicy::Feature::WebShare, document);
}

bool Navigator::canShare(Document& document, const ShareData& data)
{
    if (!document.isFullyActive() || !validateWebSharePolicy(document))
        return false;

    bool hasShareableFiles = document.settings().webShareFileAPIEnabled() && !data.files.isEmpty();

    if (data.title.isNull() && data.text.isNull() && data.url.isNull() && !hasShareableFiles)
        return false;

    return data.url.isNull() || shareableURLForShareData(document, data);
}

void Navigator::share(Document& document, const ShareData& data, Ref<DeferredPromise>&& promise)
{
    if (!document.isFullyActive()) {
        promise->reject(ExceptionCode::InvalidStateError);
        return;
    }

    if (!validateWebSharePolicy(document)) {
        promise->reject(ExceptionCode::NotAllowedError, "Third-party iframes are not allowed to call share() unless explicitly allowed via Feature-Policy (web-share)"_s);
        return;
    }

    if (m_hasPendingShare) {
        promise->reject(ExceptionCode::InvalidStateError, "share() is already in progress"_s);
        return;
    }

    RefPtr window = this->window();
    if (!window || !window->consumeTransientActivation()) {
        promise->reject(ExceptionCode::NotAllowedError);
        return;
    }

    if (!canShare(document, data)) {
        promise->reject(ExceptionCode::TypeError);
        return;
    }

    std::optional<URL> url = shareableURLForShareData(document, data);
    ShareDataWithParsedURL shareData = {
        data,
        url,
        { },
        ShareDataOriginator::Web,
    };
    if (document.settings().webShareFileAPIEnabled() && !data.files.isEmpty()) {
        if (m_loader)
            m_loader->cancel();

        m_loader = ShareDataReader::create([this, promise = WTFMove(promise)] (ExceptionOr<ShareDataWithParsedURL&> readData) mutable {
            showShareData(readData, WTFMove(promise));
        });
        m_loader->start(&document, WTFMove(shareData));
        return;
    }
    this->showShareData(shareData, WTFMove(promise));
}

void Navigator::showShareData(ExceptionOr<ShareDataWithParsedURL&> readData, Ref<DeferredPromise>&& promise)
{
    if (readData.hasException()) {
        promise->reject(readData.releaseException());
        return;
    }
    
    RefPtr frame = this->frame();
    if (!frame || !frame->page())
        return;

    m_hasPendingShare = true;

    if (frame->page()->isControlledByAutomation()) {
        RunLoop::mainSingleton().dispatch([promise = WTFMove(promise), weakThis = WeakPtr { *this }] {
            if (weakThis)
                weakThis->m_hasPendingShare = false;
            promise->resolve();
        });
        return;
    }
    
    auto shareData = readData.returnValue();
    
    frame->page()->chrome().showShareSheet(WTFMove(shareData), [promise = WTFMove(promise), weakThis = WeakPtr { *this }] (bool completed) {
        if (weakThis)
            weakThis->m_hasPendingShare = false;
        if (completed) {
            promise->resolve();
            return;
        }
        promise->reject(Exception { ExceptionCode::AbortError, "Abort due to cancellation of share."_s });
    });
}

// https://html.spec.whatwg.org/multipage/system-state.html#pdf-viewing-support
// Section 8.9.1.6 states that if pdfViewerEnabled is true, we must return a list
// of exactly five PDF view plugins, in a particular order.
constexpr ASCIILiteral genericPDFViewerName { "PDF Viewer"_s };

static const Vector<String>& dummyPDFPluginNames()
{
    static NeverDestroyed<Vector<String>> dummyPluginNames(std::initializer_list<String> {
        genericPDFViewerName,
        "Chrome PDF Viewer"_s,
        "Chromium PDF Viewer"_s,
        "Microsoft Edge PDF Viewer"_s,
        "WebKit built-in PDF"_s,
    });
    return dummyPluginNames;
}

void Navigator::initializePluginAndMimeTypeArrays()
{
    if (m_plugins)
        return;

    RefPtr frame = this->frame();
    bool needsEmptyNavigatorPluginsQuirk = frame && frame->document() && frame->document()->quirks().shouldNavigatorPluginsBeEmpty();
    if (!frame || !frame->page() || needsEmptyNavigatorPluginsQuirk) {
        if (needsEmptyNavigatorPluginsQuirk)
            frame->protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Info, "QUIRK: Navigator plugins / mimeTypes empty on marcus.com. More information at https://bugs.webkit.org/show_bug.cgi?id=248798"_s);
        m_plugins = DOMPluginArray::create(*this);
        m_mimeTypes = DOMMimeTypeArray::create(*this);
        return;
    }

    m_pdfViewerEnabled = frame->loader().client().canShowMIMEType("application/pdf"_s);
    if (!m_pdfViewerEnabled) {
        m_plugins = DOMPluginArray::create(*this);
        m_mimeTypes = DOMMimeTypeArray::create(*this);
        return;
    }

    // macOS uses a PDF Plugin (which may be disabled). Other ports handle PDF's through native
    // platform views outside the engine, or use pdf.js.
    PluginInfo pdfPluginInfo = frame->page()->pluginData().builtInPDFPlugin().value_or(PluginData::dummyPDFPluginInfo());

    Vector<Ref<DOMPlugin>> domPlugins;
    Vector<Ref<DOMMimeType>> domMimeTypes;

    // https://html.spec.whatwg.org/multipage/system-state.html#pdf-viewing-support
    // Section 8.9.1.6 states that if pdfViewerEnabled is true, we must return a list
    // of exactly five PDF view plugins, in a particular order. They also must return
    // a specific plain English string for 'Navigator.plugins[x].description':
    constexpr auto navigatorPDFDescription = "Portable Document Format"_s;
    for (auto& currentDummyName : dummyPDFPluginNames()) {
        pdfPluginInfo.name = currentDummyName;
        pdfPluginInfo.desc = navigatorPDFDescription;
        domPlugins.append(DOMPlugin::create(*this, pdfPluginInfo));
        
        // Register the copy of the PluginInfo using the generic 'PDF Viewer' name
        // as the handler for PDF MIME type to match the specification.
        if (currentDummyName == genericPDFViewerName)
            domMimeTypes.appendVector(domPlugins.last()->mimeTypes());
    }

    m_plugins = DOMPluginArray::create(*this, WTFMove(domPlugins));
    m_mimeTypes = DOMMimeTypeArray::create(*this, WTFMove(domMimeTypes));
}

DOMPluginArray& Navigator::plugins()
{
    if (RefPtr frame = this->frame(); frame && frame->settings().webAPIStatisticsEnabled())
        ResourceLoadObserver::shared().logNavigatorAPIAccessed(*frame->protectedDocument(), NavigatorAPIsAccessed::Plugins);

    initializePluginAndMimeTypeArrays();
    return *m_plugins;
}

DOMMimeTypeArray& Navigator::mimeTypes()
{
    if (RefPtr frame = this->frame(); frame && frame->settings().webAPIStatisticsEnabled())
        ResourceLoadObserver::shared().logNavigatorAPIAccessed(*frame->protectedDocument(), NavigatorAPIsAccessed::MimeTypes);

    initializePluginAndMimeTypeArrays();
    return *m_mimeTypes;
}

bool Navigator::pdfViewerEnabled()
{
    // https://html.spec.whatwg.org/multipage/system-state.html#pdf-viewing-support
    initializePluginAndMimeTypeArrays();
    return m_pdfViewerEnabled;
}

bool Navigator::cookieEnabled() const
{
    RefPtr frame = this->frame();
    if (!frame)
        return false;

    if (frame->settings().webAPIStatisticsEnabled())
        ResourceLoadObserver::shared().logNavigatorAPIAccessed(*frame->protectedDocument(), NavigatorAPIsAccessed::CookieEnabled);

    RefPtr page = frame->page();
    if (!page)
        return false;

    if (!page->settings().cookieEnabled())
        return false;

    RefPtr document = frame->document();
    if (!document)
        return false;

    return page->cookieJar().cookiesEnabled(*document);
}

#if ENABLE(NAVIGATOR_STANDALONE)

bool Navigator::standalone() const
{
    RefPtr frame = this->frame();
    return frame && frame->settings().standalone();
}

#endif

GPU* Navigator::gpu()
{
#if HAVE(WEBGPU_IMPLEMENTATION)
    if (!m_gpuForWebGPU) {
        RefPtr frame = this->frame();
        if (!frame)
            return nullptr;
        if (!frame->settings().webGPUEnabled())
            return nullptr;
        RefPtr page = frame->page();
        if (!page)
            return nullptr;
        RefPtr gpu = page->chrome().createGPUForWebGPU();
        if (!gpu)
            return nullptr;

        m_gpuForWebGPU = GPU::create(*gpu);
    }
#endif

    return m_gpuForWebGPU.get();
}

Page* Navigator::page()
{
    RefPtr frame = this->frame();
    return frame ? frame->page() : nullptr;
}

RefPtr<Page> Navigator::protectedPage()
{
    return page();
}

const Document* Navigator::document() const
{
    RefPtr frame = this->frame();
    return frame ? frame->document() : nullptr;
}

Document* Navigator::document()
{
    RefPtr frame = this->frame();
    return frame ? frame->document() : nullptr;
}

RefPtr<Document> Navigator::protectedDocument()
{
    return document();
}

void Navigator::setAppBadge(std::optional<unsigned long long> badge, Ref<DeferredPromise>&& promise)
{
    RefPtr frame = this->frame();
    if (!frame) {
        promise->reject(ExceptionCode::InvalidStateError);
        return;
    }

    RefPtr page = frame->page();
    if (!page) {
        promise->reject(ExceptionCode::InvalidStateError);
        return;
    }

    RefPtr document = frame->document();
    if (document && !document->isFullyActive()) {
        promise->reject(ExceptionCode::InvalidStateError);
        return;
    }

    page->badgeClient().setAppBadge(frame.get(), SecurityOriginData::fromFrame(frame.get()), badge);
    promise->resolve();
}

void Navigator::clearAppBadge(Ref<DeferredPromise>&& promise)
{
    setAppBadge(0, WTFMove(promise));
}

int Navigator::maxTouchPoints() const
{
#if ENABLE(IOS_TOUCH_EVENTS) && !PLATFORM(MACCATALYST)
    RefPtr document = this->document();
    if (!document || !document->quirks().needsZeroMaxTouchPointsQuirk())
        return 5;
#endif

    return 0;
}

void Navigator::initializeNavigatorUAData() const
{
    if (m_navigatorUAData)
        return;

    // FIXME(296489): populate the data structure
    return;
}

NavigatorUAData& Navigator::userAgentData() const
{
    if (!m_navigatorUAData)
        initializeNavigatorUAData();

    return *m_navigatorUAData;
};

} // namespace WebCore
