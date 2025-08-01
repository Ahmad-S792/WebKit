/*
 * Copyright (C) 2014-2025 Apple Inc. All rights reserved.
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
#include "WebUserContentController.h"

#include "ContentWorldShared.h"
#include "FrameInfoData.h"
#include "InjectUserScriptImmediately.h"
#include "InjectedBundleScriptWorld.h"
#include "JavaScriptEvaluationResult.h"
#include "ScriptMessageHandlerIdentifier.h"
#include "WebCompiledContentRuleList.h"
#include "WebFrame.h"
#include "WebPage.h"
#include "WebProcess.h"
#include "WebUserContentControllerMessages.h"
#include "WebUserContentControllerProxyMessages.h"
#include <JavaScriptCore/APICast.h>
#include <JavaScriptCore/JSContextRef.h>
#include <JavaScriptCore/JSRetainPtr.h>
#include <WebCore/DOMWrapperWorld.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/Page.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/SerializedScriptValue.h>
#include <WebCore/UserStyleSheet.h>
#include <wtf/NeverDestroyed.h>

#if ENABLE(USER_MESSAGE_HANDLERS)
#include <WebCore/UserMessageHandler.h>
#include <WebCore/UserMessageHandlerDescriptor.h>
#endif

namespace WebKit {
using namespace WebCore;

static HashMap<UserContentControllerIdentifier, WeakPtr<WebUserContentController>>& userContentControllers()
{
    static NeverDestroyed<HashMap<UserContentControllerIdentifier, WeakPtr<WebUserContentController>>> userContentControllers;

    return userContentControllers;
}

typedef HashMap<ContentWorldIdentifier, std::pair<Ref<InjectedBundleScriptWorld>, unsigned>> WorldMap;

static WorldMap& worldMap()
{
    static NeverDestroyed<WorldMap> map(std::initializer_list<WorldMap::KeyValuePairType> { { pageContentWorldIdentifier(), std::make_pair(Ref { InjectedBundleScriptWorld::normalWorldSingleton() }, 1) } });

    return map;
}

Ref<WebUserContentController> WebUserContentController::getOrCreate(UserContentControllerIdentifier identifier)
{
    auto& userContentControllerPtr = userContentControllers().add(identifier, nullptr).iterator->value;
    if (userContentControllerPtr)
        return *userContentControllerPtr;

    RefPtr<WebUserContentController> userContentController = adoptRef(new WebUserContentController(identifier));
    userContentControllerPtr = userContentController.get();

    return userContentController.releaseNonNull();
}

WebUserContentController::WebUserContentController(UserContentControllerIdentifier identifier)
    : m_identifier(identifier)
{
    WebProcess::singleton().addMessageReceiver(Messages::WebUserContentController::messageReceiverName(), m_identifier, *this);
}

WebUserContentController::~WebUserContentController()
{
    ASSERT(userContentControllers().contains(m_identifier));

    WebProcess::singleton().removeMessageReceiver(Messages::WebUserContentController::messageReceiverName(), m_identifier);

    userContentControllers().remove(m_identifier);
}

InjectedBundleScriptWorld* WebUserContentController::worldForIdentifier(ContentWorldIdentifier identifier)
{
    auto iterator = worldMap().find(identifier);
    return iterator == worldMap().end() ? nullptr : iterator->value.first.ptr();
}

InjectedBundleScriptWorld* WebUserContentController::addContentWorld(const ContentWorldData& world)
{
    if (world.identifier == pageContentWorldIdentifier())
        return nullptr;

    auto addResult = worldMap().ensure(world.identifier, [&] {
#if PLATFORM(GTK) || PLATFORM(WPE)
        // The GLib API doesn't allow to create script worlds from the UI process. We need to
        // use the existing world created by the web extension if any. The world name is used
        // as the identifier.
        if (auto* existingWorld = InjectedBundleScriptWorld::find(world.name))
            return std::make_pair(Ref<InjectedBundleScriptWorld>(*existingWorld), 1);
#endif
        return std::make_pair(InjectedBundleScriptWorld::create(world.name, InjectedBundleScriptWorld::Type::User), 1);
    });

    if (addResult.isNewEntry) {
        Ref scriptWorld = addResult.iterator->value.first;
        if (world.options.contains(ContentWorldOption::AllowAccessToClosedShadowRoots))
            scriptWorld->makeAllShadowRootsOpen();
        if (world.options.contains(ContentWorldOption::AllowAutofill))
            scriptWorld->setAllowAutofill();
        if (world.options.contains(ContentWorldOption::AllowElementUserInfo))
            scriptWorld->setAllowElementUserInfo();
        if (world.options.contains(ContentWorldOption::DisableLegacyBuiltinOverrides))
            scriptWorld->disableOverrideBuiltinsBehavior();
        if (world.options.contains(ContentWorldOption::AllowNodeInfo))
            scriptWorld->setNodeInfoEnabled();
        return scriptWorld.ptr();
    }
    return nullptr;
}

void WebUserContentController::addContentWorlds(const Vector<ContentWorldData>& worlds)
{
    for (auto& world : worlds) {
        if (RefPtr contentWorld = addContentWorld(world)) {
            Page::forEachPage([&] (auto& page) {
                if (&page.userContentProvider() != this)
                    return;

                Ref mainFrame = page.mainFrame();
                for (RefPtr frame = mainFrame.ptr(); frame; frame = frame->tree().traverseNext()) {
                    RefPtr localFrame = dynamicDowncast<LocalFrame>(frame);
                    if (!localFrame)
                        continue;
                    localFrame->loader().client().dispatchGlobalObjectAvailable(contentWorld->coreWorld());
                }
            });
        }
    }

}

void WebUserContentController::removeContentWorlds(const Vector<ContentWorldIdentifier>& worldIdentifiers)
{
    for (auto& worldIdentifier : worldIdentifiers) {
        ASSERT(worldIdentifier != pageContentWorldIdentifier());

        auto it = worldMap().find(worldIdentifier);
        if (it == worldMap().end()) {
            WTFLogAlways("Trying to remove a ContentWorld (id=%" PRIu64 ") that does not exist.", worldIdentifier.toUInt64());
            return;
        }

        it->value.second--;
        
        if (!it->value.second)
            worldMap().remove(it);
    }
}

void WebUserContentController::addUserScripts(Vector<WebUserScriptData>&& userScripts, InjectUserScriptImmediately immediately)
{
    for (const auto& userScriptData : userScripts) {
        auto it = worldMap().find(userScriptData.worldIdentifier);
        if (it == worldMap().end()) {
            WTFLogAlways("Trying to add a UserScript to a ContentWorld (id=%" PRIu64 ") that does not exist.", userScriptData.worldIdentifier.toUInt64());
            continue;
        }

        UserScript script = userScriptData.userScript;
        Ref world = it->value.first;
        addUserScriptInternal(world, userScriptData.identifier, WTFMove(script), immediately);
    }
}

void WebUserContentController::removeUserScript(ContentWorldIdentifier worldIdentifier, UserScriptIdentifier userScriptIdentifier)
{
    auto it = worldMap().find(worldIdentifier);
    if (it == worldMap().end()) {
        WTFLogAlways("Trying to remove a UserScript from a ContentWorld (id=%" PRIu64 ") that does not exist.", worldIdentifier.toUInt64());
        return;
    }

    Ref world = it->value.first;
    removeUserScriptInternal(world, userScriptIdentifier);
}

void WebUserContentController::removeAllUserScripts(const Vector<ContentWorldIdentifier>& worldIdentifiers)
{
    for (auto& worldIdentifier : worldIdentifiers) {
        auto it = worldMap().find(worldIdentifier);
        if (it == worldMap().end()) {
            WTFLogAlways("Trying to remove all UserScripts from a ContentWorld (id=%" PRIu64 ") that does not exist.", worldIdentifier.toUInt64());
            return;
        }

        Ref world = it->value.first;
        removeUserScripts(world);
    }
}

void WebUserContentController::addUserStyleSheets(const Vector<WebUserStyleSheetData>& userStyleSheets)
{
    for (const auto& userStyleSheetData : userStyleSheets) {
        auto it = worldMap().find(userStyleSheetData.worldIdentifier);
        if (it == worldMap().end()) {
            WTFLogAlways("Trying to add a UserStyleSheet to a ContentWorld (id=%" PRIu64 ") that does not exist.", userStyleSheetData.worldIdentifier.toUInt64());
            continue;
        }
        
        UserStyleSheet sheet = userStyleSheetData.userStyleSheet;
        Ref world = it->value.first;
        addUserStyleSheetInternal(world, userStyleSheetData.identifier, WTFMove(sheet));
    }

    invalidateInjectedStyleSheetCacheInAllFramesInAllPages();
}

void WebUserContentController::removeUserStyleSheet(ContentWorldIdentifier worldIdentifier, UserStyleSheetIdentifier userStyleSheetIdentifier)
{
    auto it = worldMap().find(worldIdentifier);
    if (it == worldMap().end()) {
        WTFLogAlways("Trying to remove a UserStyleSheet from a ContentWorld (id=%" PRIu64 ") that does not exist.", worldIdentifier.toUInt64());
        return;
    }

    Ref world = it->value.first;
    removeUserStyleSheetInternal(world, userStyleSheetIdentifier);
}

void WebUserContentController::removeAllUserStyleSheets(const Vector<ContentWorldIdentifier>& worldIdentifiers)
{
    bool sheetsChanged = false;
    for (auto& worldIdentifier : worldIdentifiers) {
        auto it = worldMap().find(worldIdentifier);
        if (it == worldMap().end()) {
            WTFLogAlways("Trying to remove all UserStyleSheets from a ContentWorld (id=%" PRIu64 ") that does not exist.", worldIdentifier.toUInt64());
            return;
        }

        Ref world = it->value.first;
        if (m_userStyleSheets.remove(world.ptr()))
            sheetsChanged = true;
    }

    if (sheetsChanged)
        invalidateInjectedStyleSheetCacheInAllFramesInAllPages();
}

#if ENABLE(USER_MESSAGE_HANDLERS)
class WebUserMessageHandlerDescriptorProxy : public WebCore::UserMessageHandlerDescriptor {
public:
    static Ref<WebUserMessageHandlerDescriptorProxy> create(WebUserContentController* controller, const AtomString& name, InjectedBundleScriptWorld& world, ScriptMessageHandlerIdentifier identifier)
    {
        return adoptRef(*new WebUserMessageHandlerDescriptorProxy(controller, name, world, identifier));
    }

    virtual ~WebUserMessageHandlerDescriptorProxy()
    {
    }

    ScriptMessageHandlerIdentifier identifier() { return m_identifier; }

private:
    WebUserMessageHandlerDescriptorProxy(WebUserContentController* controller, const AtomString& name, InjectedBundleScriptWorld& world, ScriptMessageHandlerIdentifier identifier)
        : WebCore::UserMessageHandlerDescriptor(name, world.coreWorld())
        , m_controller(controller)
        , m_identifier(identifier)
    {
    }

    // WebCore::UserMessageHandlerDescriptor
    void didPostMessage(WebCore::UserMessageHandler& handler, JSC::JSGlobalObject& globalObject, JSC::JSValue jsMessage, WTF::Function<void(JSC::JSValue, const String&)>&& completionHandler) override
    {
        RefPtr frame = handler.frame();
        if (!frame)
            return;
    
        auto webFrame = WebFrame::fromCoreFrame(*frame);
        if (!webFrame)
            return;

        RefPtr webPage = webFrame->page();
        if (!webPage)
            return;

        JSRetainPtr context { JSContextGetGlobalContext(toRef(&globalObject)) };
        auto message = JavaScriptEvaluationResult::extract(context.get(), toRef(&globalObject, jsMessage));
        if (!message)
            return;

        WebProcess::singleton().protectedParentProcessConnection()->sendWithAsyncReply(Messages::WebUserContentControllerProxy::DidPostMessage(webPage->webPageProxyIdentifier(), webFrame->info(), m_identifier, *message), [completionHandler = WTFMove(completionHandler), context](Expected<WebKit::JavaScriptEvaluationResult, String>&& result) {
            if (!result)
                return completionHandler(JSC::jsUndefined(), result.error());
            completionHandler(toJS(toJS(context.get()), result->toJS(context.get()).get()), { });
        }, m_controller->identifier());
    }

    RefPtr<WebUserContentController> m_controller;
    ScriptMessageHandlerIdentifier m_identifier;
};
#endif

void WebUserContentController::addUserScriptMessageHandlers(const Vector<WebScriptMessageHandlerData>& scriptMessageHandlers)
{
#if ENABLE(USER_MESSAGE_HANDLERS)
    for (auto& handler : scriptMessageHandlers) {
        auto it = worldMap().find(handler.worldIdentifier);
        if (it == worldMap().end()) {
            WTFLogAlways("Trying to add a UserScriptMessageHandler to a ContentWorld (id=%" PRIu64 ") that does not exist.", handler.worldIdentifier.toUInt64());
            continue;
        }

        Ref world = it->value.first;
        addUserScriptMessageHandlerInternal(world, handler.identifier, AtomString(handler.name));
    }
#else
    UNUSED_PARAM(scriptMessageHandlers);
#endif
}

void WebUserContentController::removeUserScriptMessageHandler(ContentWorldIdentifier worldIdentifier, ScriptMessageHandlerIdentifier userScriptMessageHandlerIdentifier)
{
#if ENABLE(USER_MESSAGE_HANDLERS)
    auto it = worldMap().find(worldIdentifier);
    if (it == worldMap().end()) {
        WTFLogAlways("Trying to remove a UserScriptMessageHandler from a ContentWorld (id=%" PRIu64 ") that does not exist.", worldIdentifier.toUInt64());
        return;
    }

    Ref world = it->value.first;
    removeUserScriptMessageHandlerInternal(world, userScriptMessageHandlerIdentifier);
#else
    UNUSED_PARAM(worldIdentifier);
    UNUSED_PARAM(userScriptMessageHandlerIdentifier);
#endif
}

void WebUserContentController::removeAllUserScriptMessageHandlers()
{
#if ENABLE(USER_MESSAGE_HANDLERS)
    if (m_userMessageHandlers.isEmpty())
        return;

    m_userMessageHandlers.clear();
    invalidateAllRegisteredUserMessageHandlerInvalidationClients();
#endif
}

void WebUserContentController::removeAllUserScriptMessageHandlersForWorlds(const Vector<ContentWorldIdentifier>& worldIdentifiers)
{
#if ENABLE(USER_MESSAGE_HANDLERS)
    bool userMessageHandlersChanged = false;
    for (auto& worldIdentifier : worldIdentifiers) {
        auto it = worldMap().find(worldIdentifier);
        if (it == worldMap().end()) {
            WTFLogAlways("Trying to remove all UserScriptMessageHandler from a ContentWorld (id=%" PRIu64 ") that does not exist.", worldIdentifier.toUInt64());
            return;
        }

        Ref world = it->value.first;
        if (m_userMessageHandlers.remove(world.ptr()))
            userMessageHandlersChanged = true;
    }

    if (userMessageHandlersChanged)
        invalidateAllRegisteredUserMessageHandlerInvalidationClients();
#else
    UNUSED_PARAM(worldIdentifiers);
#endif
}

#if ENABLE(USER_MESSAGE_HANDLERS)
void WebUserContentController::addUserScriptMessageHandlerInternal(InjectedBundleScriptWorld& world, ScriptMessageHandlerIdentifier userScriptMessageHandlerIdentifier, const AtomString& name)
{
    auto& messageHandlersInWorld = m_userMessageHandlers.ensure(&world, [] { return Vector<std::pair<ScriptMessageHandlerIdentifier, RefPtr<WebUserMessageHandlerDescriptorProxy>>> { }; }).iterator->value;
    if (messageHandlersInWorld.findIf([&](auto& pair) { return pair.first ==  userScriptMessageHandlerIdentifier; }) != notFound)
        return;
    messageHandlersInWorld.append(std::make_pair(userScriptMessageHandlerIdentifier, WebUserMessageHandlerDescriptorProxy::create(this, name, world, userScriptMessageHandlerIdentifier)));
}

void WebUserContentController::removeUserScriptMessageHandlerInternal(InjectedBundleScriptWorld& world, ScriptMessageHandlerIdentifier userScriptMessageHandlerIdentifier)
{
    auto it = m_userMessageHandlers.find(&world);
    if (it == m_userMessageHandlers.end())
        return;

    Ref protectedThis { *this };

    auto& userMessageHandlers = it->value;
    bool userMessageHandlersChanged = userMessageHandlers.removeFirstMatching([userScriptMessageHandlerIdentifier](auto& pair) {
        return pair.first ==  userScriptMessageHandlerIdentifier;
    });

    if (!userMessageHandlersChanged)
        return;

    if (userMessageHandlers.isEmpty())
        m_userMessageHandlers.remove(it);

    invalidateAllRegisteredUserMessageHandlerInvalidationClients();
}
#endif

#if ENABLE(CONTENT_EXTENSIONS)
void WebUserContentController::addContentRuleLists(Vector<std::pair<WebCompiledContentRuleListData, URL>>&& contentRuleLists)
{
    for (auto&& pair : contentRuleLists) {
        auto&& contentRuleList = WTFMove(pair.first);
        String identifier = contentRuleList.identifier;
        if (RefPtr compiledContentRuleList = WebCompiledContentRuleList::create(WTFMove(contentRuleList)))
            m_contentExtensionBackend.addContentExtension(identifier, compiledContentRuleList.releaseNonNull(), WTFMove(pair.second));
    }
}

void WebUserContentController::removeContentRuleList(const String& name)
{
    m_contentExtensionBackend.removeContentExtension(name);
}

void WebUserContentController::removeAllContentRuleLists()
{
    m_contentExtensionBackend.removeAllContentExtensions();
}
#endif

void WebUserContentController::addUserScriptInternal(InjectedBundleScriptWorld& world, const std::optional<UserScriptIdentifier>& userScriptIdentifier, UserScript&& userScript, InjectUserScriptImmediately immediately)
{
    if (immediately == InjectUserScriptImmediately::Yes) {
        Page::forEachPage([&] (auto& page) {
            if (&page.userContentProvider() != this)
                return;

            if (userScript.injectedFrames() == UserContentInjectedFrames::InjectInTopFrameOnly) {
                if (RefPtr localMainFrame = page.localMainFrame())
                    localMainFrame->injectUserScriptImmediately(world.coreWorld(), userScript);
                return;
            }

            Ref mainFrame { page.mainFrame() };
            for (RefPtr frame = mainFrame.ptr(); frame; frame = frame->tree().traverseNext(mainFrame.ptr())) {
                RefPtr localFrame = dynamicDowncast<LocalFrame>(frame);
                if (!localFrame)
                    continue;
                Ref coreWorld = world.coreWorld();
                localFrame->injectUserScriptImmediately(coreWorld, userScript);
            }
        });
    }

    auto& scriptsInWorld = m_userScripts.ensure(&world, [] { return Vector<std::pair<std::optional<UserScriptIdentifier>, WebCore::UserScript>>(); }).iterator->value;
    if (userScriptIdentifier && scriptsInWorld.findIf([&](auto& pair) { return pair.first == userScriptIdentifier; }) != notFound)
        return;

    scriptsInWorld.append(std::make_pair(userScriptIdentifier, WTFMove(userScript)));
}

void WebUserContentController::addUserScript(InjectedBundleScriptWorld& world, UserScript&& userScript)
{
    addUserScriptInternal(world, std::nullopt, WTFMove(userScript), InjectUserScriptImmediately::No);
}

void WebUserContentController::removeUserScriptWithURL(InjectedBundleScriptWorld& world, const URL& url)
{
    auto it = m_userScripts.find(&world);
    if (it == m_userScripts.end())
        return;

    auto& scripts = it->value;
    scripts.removeAllMatching([&](auto& pair) {
        return pair.second.url() == url;
    });

    if (scripts.isEmpty())
        m_userScripts.remove(it);
}

void WebUserContentController::removeUserScriptInternal(InjectedBundleScriptWorld& world, UserScriptIdentifier userScriptIdentifier)
{
    auto it = m_userScripts.find(&world);
    if (it == m_userScripts.end())
        return;

    auto& scripts = it->value;
    scripts.removeFirstMatching([userScriptIdentifier](auto& pair) {
        return pair.first == userScriptIdentifier;
    });

    if (scripts.isEmpty())
        m_userScripts.remove(it);
}

void WebUserContentController::removeUserScripts(InjectedBundleScriptWorld& world)
{
    m_userScripts.remove(&world);
}

void WebUserContentController::addUserStyleSheetInternal(InjectedBundleScriptWorld& world, const std::optional<UserStyleSheetIdentifier>& userStyleSheetIdentifier, UserStyleSheet&& userStyleSheet)
{
    auto& styleSheetsInWorld = m_userStyleSheets.ensure(&world, [] { return Vector<std::pair<std::optional<UserStyleSheetIdentifier>, WebCore::UserStyleSheet>>(); }).iterator->value;
    if (userStyleSheetIdentifier && styleSheetsInWorld.findIf([&](auto& pair) { return pair.first == userStyleSheetIdentifier; }) != notFound)
        return;

    if (auto pageID = userStyleSheet.pageID()) {
        if (RefPtr webPage = WebProcess::singleton().webPage(*pageID)) {
            if (RefPtr page = webPage->corePage())
                page->injectUserStyleSheet(userStyleSheet);
        }
    }

    styleSheetsInWorld.append(std::make_pair(userStyleSheetIdentifier, WTFMove(userStyleSheet)));
}

void WebUserContentController::addUserStyleSheet(InjectedBundleScriptWorld& world, UserStyleSheet&& userStyleSheet)
{
    addUserStyleSheetInternal(world, std::nullopt, WTFMove(userStyleSheet));
    invalidateInjectedStyleSheetCacheInAllFramesInAllPages();
}

void WebUserContentController::removeUserStyleSheetWithURL(InjectedBundleScriptWorld& world, const URL& url)
{
    auto it = m_userStyleSheets.find(&world);
    if (it == m_userStyleSheets.end())
        return;

    auto& stylesheets = it->value;
    bool sheetsChanged = stylesheets.removeAllMatching([&](auto& pair) {
        return pair.second.url() == url;
    });

    if (!sheetsChanged)
        return;

    if (stylesheets.isEmpty())
        m_userStyleSheets.remove(it);

    invalidateInjectedStyleSheetCacheInAllFramesInAllPages();
}

void WebUserContentController::removeUserStyleSheetInternal(InjectedBundleScriptWorld& world, UserStyleSheetIdentifier userStyleSheetIdentifier)
{
    auto it = m_userStyleSheets.find(&world);
    if (it == m_userStyleSheets.end())
        return;

    auto& stylesheets = it->value;

    bool sheetsChanged = stylesheets.removeFirstMatching([&](auto& pair) {
        if (pair.first != userStyleSheetIdentifier)
            return false;

        auto& userStyleSheet = pair.second;
        if (auto pageID = userStyleSheet.pageID()) {
            if (RefPtr webPage = WebProcess::singleton().webPage(*pageID)) {
                if (RefPtr page = webPage->corePage())
                    page->removeInjectedUserStyleSheet(userStyleSheet);
            }
        }
        return true;
    });

    if (!sheetsChanged)
        return;

    if (stylesheets.isEmpty())
        m_userStyleSheets.remove(it);

    invalidateInjectedStyleSheetCacheInAllFramesInAllPages();
}

void WebUserContentController::removeUserStyleSheets(InjectedBundleScriptWorld& world)
{
    if (!m_userStyleSheets.remove(&world))
        return;

    invalidateInjectedStyleSheetCacheInAllFramesInAllPages();
}

void WebUserContentController::removeAllUserContent()
{
    m_userScripts.clear();

    if (!m_userStyleSheets.isEmpty()) {
        m_userStyleSheets.clear();
        invalidateInjectedStyleSheetCacheInAllFramesInAllPages();
    }
}

void WebUserContentController::forEachUserScript(NOESCAPE const Function<void(WebCore::DOMWrapperWorld&, const WebCore::UserScript&)>& functor) const
{
    for (const auto& worldAndUserScriptVector : m_userScripts) {
        RefPtr key = worldAndUserScriptVector.key;
        Ref world = key->coreWorld();
        for (const auto& identifierUserScriptPair : worldAndUserScriptVector.value)
            functor(world, identifierUserScriptPair.second);
    }
}

void WebUserContentController::forEachUserStyleSheet(NOESCAPE const Function<void(const WebCore::UserStyleSheet&)>& functor) const
{
    for (auto& styleSheetVector : m_userStyleSheets.values()) {
        for (const auto& identifierUserStyleSheetPair : styleSheetVector)
            functor(identifierUserStyleSheetPair.second);
    }
}

#if ENABLE(USER_MESSAGE_HANDLERS)
void WebUserContentController::forEachUserMessageHandler(NOESCAPE const Function<void(const WebCore::UserMessageHandlerDescriptor&)>& functor) const
{
    for (auto& userMessageHandlerVector : m_userMessageHandlers.values()) {
        for (auto& pair : userMessageHandlerVector)
            functor(*pair.second.get());
    }
}
#endif

} // namespace WebKit
