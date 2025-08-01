/*
 * Copyright (C) 2012-2023 Apple Inc. All rights reserved.
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

#import "config.h"
#import "LegacyCustomProtocolManager.h"

#import "CacheStoragePolicy.h"
#import "LegacyCustomProtocolManagerMessages.h"
#import "NetworkProcess.h"
#import <Foundation/NSURLSession.h>
#import <WebCore/ResourceError.h>
#import <WebCore/ResourceRequest.h>
#import <WebCore/ResourceResponse.h>
#import <pal/spi/cocoa/NSURLConnectionSPI.h>
#import <pal/text/TextEncoding.h>
#import <wtf/URL.h>
#import <wtf/cocoa/SpanCocoa.h>

using namespace WebKit;

static RefPtr<NetworkProcess>& firstNetworkProcess()
{
    static NeverDestroyed<RefPtr<NetworkProcess>> networkProcess;
    return networkProcess.get();
}

static RefPtr<NetworkProcess> protectedFirstNetworkProcess()
{
    return firstNetworkProcess();
}

void LegacyCustomProtocolManager::networkProcessCreated(NetworkProcess& networkProcess)
{
    auto hasRegisteredSchemes = [] (auto* legacyCustomProtocolManager) {
        if (!legacyCustomProtocolManager)
            return false;
        Locker locker { legacyCustomProtocolManager->m_registeredSchemesLock };
        return !legacyCustomProtocolManager->m_registeredSchemes.isEmpty();
    };

    RELEASE_ASSERT(!firstNetworkProcess() || !hasRegisteredSchemes(RefPtr { protectedFirstNetworkProcess()->supplement<LegacyCustomProtocolManager>() }.get()));
    firstNetworkProcess() = networkProcess;
}

NS_REQUIRES_PROPERTY_DEFINITIONS
@interface WKCustomProtocol : NSURLProtocol {
@private
    Markable<LegacyCustomProtocolID> _customProtocolID;
}
@property (nonatomic, readonly) Markable<LegacyCustomProtocolID> customProtocolID;
@property (nonatomic, readonly) CFRunLoopRef initializationRunLoop;
@end

@implementation WKCustomProtocol {
    RetainPtr<CFRunLoopRef> _initializationRunLoop;
}

@synthesize customProtocolID = _customProtocolID;

+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
    // FIXME: This code runs in a dispatch queue so we can't ref NetworkProcess here.
    if (SUPPRESS_UNCOUNTED_LOCAL auto* customProtocolManager = protectedFirstNetworkProcess()->supplement<LegacyCustomProtocolManager>())
        SUPPRESS_UNCOUNTED_ARG return customProtocolManager->supportsScheme([[[request URL] scheme] lowercaseString]);
    return NO;
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request
{
    return request;
}

+ (BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b
{
    return NO;
}

- (id)initWithRequest:(NSURLRequest *)request cachedResponse:(NSCachedURLResponse *)cachedResponse client:(id<NSURLProtocolClient>)client
{
    self = [super initWithRequest:request cachedResponse:cachedResponse client:client];
    if (!self)
        return nil;

    if (RefPtr customProtocolManager = protectedFirstNetworkProcess()->supplement<LegacyCustomProtocolManager>())
        _customProtocolID = customProtocolManager->addCustomProtocol(self);
    _initializationRunLoop = CFRunLoopGetCurrent();

    return self;
}

- (CFRunLoopRef)initializationRunLoop
{
    return _initializationRunLoop.get();
}

- (void)startLoading
{
    ensureOnMainRunLoop([customProtocolID = *self.customProtocolID, request = retainPtr([self request])] {
        if (RefPtr customProtocolManager = protectedFirstNetworkProcess()->supplement<LegacyCustomProtocolManager>())
            customProtocolManager->startLoading(customProtocolID, request.get());
    });
}

- (void)stopLoading
{
    ensureOnMainRunLoop([customProtocolID = *self.customProtocolID] {
        if (RefPtr customProtocolManager = protectedFirstNetworkProcess()->supplement<LegacyCustomProtocolManager>()) {
            customProtocolManager->stopLoading(customProtocolID);
            customProtocolManager->removeCustomProtocol(customProtocolID);
        }
    });
}

@end

namespace WebKit {

void LegacyCustomProtocolManager::registerProtocolClass()
{
}

void LegacyCustomProtocolManager::registerProtocolClass(NSURLSessionConfiguration *configuration)
{
    configuration.protocolClasses = @[[WKCustomProtocol class]];
}

void LegacyCustomProtocolManager::registerScheme(const String& scheme)
{
    ASSERT(!scheme.isNull());
    Locker locker { m_registeredSchemesLock };
    m_registeredSchemes.add(scheme);
}

void LegacyCustomProtocolManager::unregisterScheme(const String& scheme)
{
    ASSERT(!scheme.isNull());
    Locker locker { m_registeredSchemesLock };
    m_registeredSchemes.remove(scheme);
}

bool LegacyCustomProtocolManager::supportsScheme(const String& scheme)
{
    if (scheme.isNull())
        return false;

    Locker locker { m_registeredSchemesLock };
    return m_registeredSchemes.contains(scheme);
}

static inline void dispatchOnInitializationRunLoop(WKCustomProtocol* protocol, void (^block)())
{
    RetainPtr<CFRunLoopRef> runloop = protocol.initializationRunLoop;
    CFRunLoopPerformBlock(runloop.get(), kCFRunLoopDefaultMode, block);
    CFRunLoopWakeUp(runloop.get());
}

void LegacyCustomProtocolManager::didFailWithError(LegacyCustomProtocolID customProtocolID, const WebCore::ResourceError& error)
{
    RetainPtr<WKCustomProtocol> protocol = protocolForID(customProtocolID);
    if (!protocol)
        return;

    RetainPtr<NSError> nsError = error.nsError();

    dispatchOnInitializationRunLoop(protocol.get(), ^ {
        [[protocol client] URLProtocol:protocol.get() didFailWithError:nsError.get()];
    });

    removeCustomProtocol(customProtocolID);
}

void LegacyCustomProtocolManager::didLoadData(LegacyCustomProtocolID customProtocolID, std::span<const uint8_t> data)
{
    RetainPtr<WKCustomProtocol> protocol = protocolForID(customProtocolID);
    if (!protocol)
        return;

    RetainPtr nsData = toNSData(data);

    dispatchOnInitializationRunLoop(protocol.get(), ^ {
        [[protocol client] URLProtocol:protocol.get() didLoadData:nsData.get()];
    });
}

void LegacyCustomProtocolManager::didReceiveResponse(LegacyCustomProtocolID customProtocolID, const WebCore::ResourceResponse& response, CacheStoragePolicy cacheStoragePolicy)
{
    RetainPtr<WKCustomProtocol> protocol = protocolForID(customProtocolID);
    if (!protocol)
        return;

    RetainPtr<NSURLResponse> nsResponse = response.nsURLResponse();

    dispatchOnInitializationRunLoop(protocol.get(), ^ {
        [[protocol client] URLProtocol:protocol.get() didReceiveResponse:nsResponse.get() cacheStoragePolicy:toNSURLCacheStoragePolicy(cacheStoragePolicy)];
    });
}

void LegacyCustomProtocolManager::didFinishLoading(LegacyCustomProtocolID customProtocolID)
{
    RetainPtr<WKCustomProtocol> protocol = protocolForID(customProtocolID);
    if (!protocol)
        return;

    dispatchOnInitializationRunLoop(protocol.get(), ^ {
        [[protocol client] URLProtocolDidFinishLoading:protocol.get()];
    });

    removeCustomProtocol(customProtocolID);
}

void LegacyCustomProtocolManager::wasRedirectedToRequest(LegacyCustomProtocolID customProtocolID, const WebCore::ResourceRequest& request, const WebCore::ResourceResponse& redirectResponse)
{
    RetainPtr<WKCustomProtocol> protocol = protocolForID(customProtocolID);
    if (!protocol)
        return;

    RetainPtr<NSURLRequest> nsRequest = request.nsURLRequest(WebCore::HTTPBodyUpdatePolicy::DoNotUpdateHTTPBody);
    RetainPtr<NSURLResponse> nsRedirectResponse = redirectResponse.nsURLResponse();

    dispatchOnInitializationRunLoop(protocol.get(), [protocol, nsRequest, nsRedirectResponse]() {
        [[protocol client] URLProtocol:protocol.get() wasRedirectedToRequest:nsRequest.get() redirectResponse:nsRedirectResponse.get()];
    });
}

RetainPtr<WKCustomProtocol> LegacyCustomProtocolManager::protocolForID(LegacyCustomProtocolID customProtocolID)
{
    Locker locker { m_customProtocolMapLock };

    CustomProtocolMap::const_iterator it = m_customProtocolMap.find(customProtocolID);
    if (it == m_customProtocolMap.end())
        return nil;
    
    ASSERT(it->value);
    return it->value;
}

} // namespace WebKit
