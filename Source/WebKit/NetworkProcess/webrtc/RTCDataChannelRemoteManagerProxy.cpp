/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "RTCDataChannelRemoteManagerProxy.h"

#if ENABLE(WEB_RTC)

#include "NetworkConnectionToWebProcess.h"
#include "RTCDataChannelRemoteManagerMessages.h"
#include "RTCDataChannelRemoteManagerProxyMessages.h"

namespace WebKit {

RTCDataChannelRemoteManagerProxy::RTCDataChannelRemoteManagerProxy(NetworkProcess& networkProcess)
    : m_queue(WorkQueue::create("RTCDataChannelRemoteManagerProxy"_s))
    , m_networkProcess(networkProcess)
{
}

void RTCDataChannelRemoteManagerProxy::registerConnectionToWebProcess(NetworkConnectionToWebProcess& connectionToWebProcess)
{
    m_queue->dispatch([this, protectedThis = Ref { *this }, identifier = connectionToWebProcess.webProcessIdentifier(), connectionID = connectionToWebProcess.connection().uniqueID(), sharedPreferences = connectionToWebProcess.sharedPreferencesForWebProcessValue()]() mutable {
        ASSERT(!m_webProcessConnections.contains(identifier));
        m_webProcessConnections.add(identifier, connectionID);
        m_sharedPreferencesForConnections.add(connectionID, WTFMove(sharedPreferences));
    });
    connectionToWebProcess.connection().addWorkQueueMessageReceiver(Messages::RTCDataChannelRemoteManagerProxy::messageReceiverName(), m_queue, *this);
}

void RTCDataChannelRemoteManagerProxy::unregisterConnectionToWebProcess(NetworkConnectionToWebProcess& connectionToWebProcess)
{
    m_queue->dispatch([this, protectedThis = Ref { *this }, identifier = connectionToWebProcess.webProcessIdentifier(), connectionID = connectionToWebProcess.connection().uniqueID()] {
        ASSERT(m_webProcessConnections.contains(identifier));
        m_webProcessConnections.remove(identifier);
        m_sharedPreferencesForConnections.remove(connectionID);
    });
    connectionToWebProcess.connection().removeWorkQueueMessageReceiver(Messages::RTCDataChannelRemoteManagerProxy::messageReceiverName());
}

void RTCDataChannelRemoteManagerProxy::sendData(WebCore::RTCDataChannelIdentifier identifier, bool isRaw, std::span<const uint8_t> data)
{
    if (auto connectionID = m_webProcessConnections.getOptional(identifier.processIdentifier()))
        IPC::Connection::send(*connectionID, Messages::RTCDataChannelRemoteManager::SendData { identifier, isRaw, data }, 0);
}

void RTCDataChannelRemoteManagerProxy::close(WebCore::RTCDataChannelIdentifier identifier)
{
    if (auto connectionID = m_webProcessConnections.getOptional(identifier.processIdentifier()))
        IPC::Connection::send(*connectionID, Messages::RTCDataChannelRemoteManager::Close { identifier }, 0);
}

void RTCDataChannelRemoteManagerProxy::changeReadyState(WebCore::RTCDataChannelIdentifier identifier, WebCore::RTCDataChannelState state)
{
    if (auto connectionID = m_webProcessConnections.getOptional(identifier.processIdentifier()))
        IPC::Connection::send(*connectionID, Messages::RTCDataChannelRemoteManager::ChangeReadyState { identifier, state }, 0);
}

void RTCDataChannelRemoteManagerProxy::receiveData(WebCore::RTCDataChannelIdentifier identifier, bool isRaw, std::span<const uint8_t> data)
{
    if (auto connectionID = m_webProcessConnections.getOptional(identifier.processIdentifier()))
        IPC::Connection::send(*connectionID, Messages::RTCDataChannelRemoteManager::ReceiveData { identifier, isRaw, data }, 0);
}

void RTCDataChannelRemoteManagerProxy::detectError(WebCore::RTCDataChannelIdentifier identifier, WebCore::RTCErrorDetailType detail, const String& message)
{
    if (auto connectionID = m_webProcessConnections.getOptional(identifier.processIdentifier()))
        IPC::Connection::send(*connectionID, Messages::RTCDataChannelRemoteManager::DetectError { identifier, detail, message }, 0);
}

void RTCDataChannelRemoteManagerProxy::bufferedAmountIsDecreasing(WebCore::RTCDataChannelIdentifier identifier, uint64_t amount)
{
    if (auto connectionID = m_webProcessConnections.getOptional(identifier.processIdentifier()))
        IPC::Connection::send(*connectionID, Messages::RTCDataChannelRemoteManager::BufferedAmountIsDecreasing { identifier, amount }, 0);
}

std::optional<SharedPreferencesForWebProcess> RTCDataChannelRemoteManagerProxy::sharedPreferencesForWebProcess(const IPC::Connection& connection)
{
    auto iterator = m_sharedPreferencesForConnections.find(connection.uniqueID());
    if (iterator != m_sharedPreferencesForConnections.end())
        return iterator->value;

    return std::nullopt;
}

}

#endif
