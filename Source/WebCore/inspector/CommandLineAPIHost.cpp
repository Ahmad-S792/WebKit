/*
 * Copyright (C) 2007-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Matt Lilek <webkit@mattlilek.com>
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CommandLineAPIHost.h"

#include "Database.h"
#include "Document.h"
#include "EventTarget.h"
#include "InspectorDOMStorageAgent.h"
#include "JSCommandLineAPIHost.h"
#include "JSDOMGlobalObject.h"
#include "JSEventListener.h"
#include "PagePasteboardContext.h"
#include "Pasteboard.h"
#include "Storage.h"
#include "WebConsoleAgent.h"
#include <JavaScriptCore/InjectedScriptBase.h>
#include <JavaScriptCore/InspectorAgent.h>
#include <JavaScriptCore/JSCInlines.h>
#include <JavaScriptCore/JSLock.h>
#include <JavaScriptCore/ObjectConstructor.h>
#include <wtf/JSONValues.h>
#include <wtf/RefPtr.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>

#if ENABLE(WEB_RTC)
#include "JSRTCPeerConnection.h"
#include "RTCLogsCallback.h"
#endif

namespace WebCore {

using namespace JSC;
using namespace Inspector;

WTF_MAKE_TZONE_ALLOCATED_IMPL(CommandLineAPIHost::InspectableObject);

Ref<CommandLineAPIHost> CommandLineAPIHost::create()
{
    return adoptRef(*new CommandLineAPIHost);
}

CommandLineAPIHost::CommandLineAPIHost()
    : m_inspectedObject(makeUnique<InspectableObject>())
{
}

CommandLineAPIHost::~CommandLineAPIHost() = default;

void CommandLineAPIHost::disconnect()
{

    m_instrumentingAgents = nullptr;
}

void CommandLineAPIHost::inspect(JSC::JSGlobalObject& lexicalGlobalObject, JSC::JSValue object, JSC::JSValue hints)
{
    if (!m_instrumentingAgents)
        return;

    auto* inspectorAgent = m_instrumentingAgents->persistentInspectorAgent();
    if (!inspectorAgent)
        return;

    auto objectValue = Inspector::toInspectorValue(&lexicalGlobalObject, object);
    if (!objectValue)
        return;

    auto hintsValue = Inspector::toInspectorValue(&lexicalGlobalObject, hints);
    if (!hintsValue)
        return;

    auto hintsObject = hintsValue->asObject();
    if (!hintsObject)
        return;

    auto remoteObject = Inspector::Protocol::BindingTraits<Inspector::Protocol::Runtime::RemoteObject>::runtimeCast(objectValue.releaseNonNull());
    inspectorAgent->inspect(WTFMove(remoteObject), hintsObject.releaseNonNull());
}

CommandLineAPIHost::EventListenersRecord CommandLineAPIHost::getEventListeners(JSGlobalObject& lexicalGlobalObject, EventTarget& target)
{
    auto* scriptExecutionContext = target.scriptExecutionContext();
    if (!scriptExecutionContext)
        return { };

    EventListenersRecord result;

    VM& vm = lexicalGlobalObject.vm();

    for (auto& eventType : target.eventTypes()) {
        Vector<CommandLineAPIHost::ListenerEntry> entries;

        for (auto& eventListener : target.eventListeners(eventType)) {
            if (!is<JSEventListener>(eventListener->callback()))
                continue;

            auto& jsListener = downcast<JSEventListener>(eventListener->callback());

            // Hide listeners from other contexts.
            if (jsListener.isolatedWorld() != &currentWorld(lexicalGlobalObject))
                continue;

            auto* function = jsListener.ensureJSFunction(*scriptExecutionContext);
            if (!function)
                continue;

            entries.append({ Strong<JSObject>(vm, function), eventListener->useCapture(), eventListener->isPassive(), eventListener->isOnce() });
        }

        if (!entries.isEmpty())
            result.append({ eventType, WTFMove(entries) });
    }

    return result;
}

#if ENABLE(WEB_RTC)
void CommandLineAPIHost::gatherRTCLogs(JSGlobalObject& globalObject, RefPtr<RTCLogsCallback>&& callback)
{
    RefPtr document = dynamicDowncast<Document>(jsCast<JSDOMGlobalObject*>(&globalObject)->scriptExecutionContext());
    if (!document)
        return;

    if (!callback) {
        document->stopGatheringRTCLogs();
        return;
    }

    document->startGatheringRTCLogs([callback = callback.releaseNonNull()] (auto&& logType, auto&& logMessage, auto&& logLevel, auto&& connection) mutable {
        ASSERT(!logType.isNull());
        ASSERT(!logMessage.isNull());

        callback->invoke({ WTFMove(logType), WTFMove(logMessage), WTFMove(logLevel), WTFMove(connection) });
    });
}
#endif

void CommandLineAPIHost::copyText(const String& text)
{
    Pasteboard::createForCopyAndPaste({ })->writePlainText(text, Pasteboard::CannotSmartReplace);
}

JSC::JSValue CommandLineAPIHost::InspectableObject::get(JSC::JSGlobalObject&)
{
    return { };
}

void CommandLineAPIHost::addInspectedObject(std::unique_ptr<CommandLineAPIHost::InspectableObject> object)
{
    m_inspectedObject = WTFMove(object);
}

JSC::JSValue CommandLineAPIHost::inspectedObject(JSC::JSGlobalObject& lexicalGlobalObject)
{
    if (!m_inspectedObject)
        return jsUndefined();

    JSC::JSLockHolder lock(&lexicalGlobalObject);
    auto scriptValue = m_inspectedObject->get(lexicalGlobalObject);
    return scriptValue ? scriptValue : jsUndefined();
}

String CommandLineAPIHost::storageId(Storage& storage)
{
    return InspectorDOMStorageAgent::storageId(storage);
}

JSValue CommandLineAPIHost::wrapper(JSGlobalObject* exec, JSDOMGlobalObject* globalObject)
{
    JSValue value = m_wrappers.getWrapper(globalObject);
    if (value)
        return value;

    JSObject* prototype = JSCommandLineAPIHost::createPrototype(exec->vm(), *globalObject);
    Structure* structure = JSCommandLineAPIHost::createStructure(exec->vm(), globalObject, prototype);
    JSCommandLineAPIHost* commandLineAPIHost = JSCommandLineAPIHost::create(structure, globalObject, Ref { *this });
    m_wrappers.addWrapper(globalObject, commandLineAPIHost);

    return commandLineAPIHost;
}

void CommandLineAPIHost::clearAllWrappers()
{
    m_wrappers.clearAllWrappers();
    m_inspectedObject = makeUnique<InspectableObject>();
}

} // namespace WebCore
