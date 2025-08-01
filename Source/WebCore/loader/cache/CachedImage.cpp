/*
    Copyright (C) 1998 Lars Knoll (knoll@mpi-hd.mpg.de)
    Copyright (C) 2001 Dirk Mueller (mueller@kde.org)
    Copyright (C) 2002 Waldo Bastian (bastian@kde.org)
    Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
    Copyright (C) 2004-2025 Apple Inc. All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "CachedImage.h"

#include "BitmapImage.h"
#include "CachedImageClient.h"
#include "CachedResourceClient.h"
#include "CachedResourceClientWalker.h"
#include "CachedResourceLoader.h"
#include "Font.h"
#include "FrameLoader.h"
#include "FrameLoaderTypes.h"
#include "ImageAdapter.h"
#include "LocalFrame.h"
#include "LocalFrameLoaderClient.h"
#include "LocalFrameView.h"
#include "MIMETypeRegistry.h"
#include "MemoryCache.h"
#include "RenderElement.h"
#include "RenderImage.h"
#include "SVGElementTypeHelpers.h"
#include "SVGImage.h"
#include "SecurityOrigin.h"
#include "Settings.h"
#include "SharedBuffer.h"
#include "SubresourceLoader.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/StdLibExtras.h>

#if PLATFORM(IOS_FAMILY)
#include "SystemMemory.h"
#endif

#if USE(CG)
#include "PDFDocumentImage.h"
#endif

#if ENABLE(MULTI_REPRESENTATION_HEIC)
#include "MultiRepresentationHEICMetrics.h"
#endif

namespace WebCore {

CachedImage::CachedImage(CachedResourceRequest&& request, PAL::SessionID sessionID, const CookieJar* cookieJar)
    : CachedResource(WTFMove(request), Type::ImageResource, sessionID, cookieJar)
    , m_updateImageDataCount(0)
    , m_isManuallyCached(false)
    , m_shouldPaintBrokenImage(true)
    , m_forceUpdateImageDataEnabledForTesting(false)
    , m_allowsOrientationOverride(true)
{
    setStatus(Unknown);
}

CachedImage::CachedImage(Image* image, PAL::SessionID sessionID, const CookieJar* cookieJar)
    : CachedResource(URL(), Type::ImageResource, sessionID, cookieJar)
    , m_image(image)
    , m_updateImageDataCount(0)
    , m_isManuallyCached(false)
    , m_shouldPaintBrokenImage(true)
    , m_forceUpdateImageDataEnabledForTesting(false)
    , m_allowsOrientationOverride(true)
{
}

CachedImage::CachedImage(const URL& url, Image* image, PAL::SessionID sessionID, const CookieJar* cookieJar, const String& domainForCachePartition)
    : CachedResource(url, Type::ImageResource, sessionID, cookieJar)
    , m_image(image)
    , m_updateImageDataCount(0)
    , m_isManuallyCached(true)
    , m_shouldPaintBrokenImage(true)
    , m_forceUpdateImageDataEnabledForTesting(false)
{
    m_resourceRequest.setDomainForCachePartition(domainForCachePartition);

    // Use the incoming URL in the response field. This ensures that code using the response directly,
    // such as origin checks for security, actually see something.
    mutableResponse().setURL(URL { url });

    setAllowsOrientationOverride(isCORSSameOrigin() || m_image->sourceURL().protocolIsData());
}

CachedImage::~CachedImage()
{
    clearImage();
}

void CachedImage::load(CachedResourceLoader& loader)
{
    m_skippingRevalidationDocument = loader.document();
    m_settings = loader.document() ? &loader.document()->settings() : nullptr;

    if (loader.shouldPerformImageLoad(url()))
        CachedResource::load(loader);
    else
        setLoading(false);
}

void CachedImage::setBodyDataFrom(const CachedResource& resource)
{
    ASSERT(resource.type() == type());
    const auto& image = downcast<const CachedImage>(resource);

    CachedResource::setBodyDataFrom(resource);

    m_image = image.m_image;
    m_imageObserver = image.m_imageObserver;
    if (m_imageObserver)
        m_imageObserver->cachedImages().add(*this);

    if (RefPtr svgImage = dynamicDowncast<SVGImage>(m_image.get()))
        m_svgImageCache = makeUnique<SVGImageCache>(svgImage.get());
}

void CachedImage::didAddClient(CachedResourceClient& client)
{
    if (m_data && !m_image && !errorOccurred()) {
        createImage();
        protectedImage()->setData(m_data.copyRef(), true);
    }

    ASSERT(client.resourceClientType() == CachedImageClient::expectedType());
    if (m_image && !m_image->isNull())
        downcast<CachedImageClient>(client).imageChanged(this);

    if (RefPtr image = m_image)
        image->startAnimationAsynchronously();

    CachedResource::didAddClient(client);
}

void CachedImage::didRemoveClient(CachedResourceClient& client)
{
    ASSERT(client.resourceClientType() == CachedImageClient::expectedType());

    m_pendingContainerContextRequests.remove(&downcast<CachedImageClient>(client));
    m_clientsWaitingForAsyncDecoding.remove(downcast<CachedImageClient>(client));

    if (m_svgImageCache)
        m_svgImageCache->removeClientFromCache(&downcast<CachedImageClient>(client));

    CachedResource::didRemoveClient(client);

    downcast<CachedImageClient>(client).didRemoveCachedImageClient(*this);
}

bool CachedImage::isClientWaitingForAsyncDecoding(const CachedImageClient& client) const
{
    return m_clientsWaitingForAsyncDecoding.contains(const_cast<CachedImageClient&>(client));
}

void CachedImage::addClientWaitingForAsyncDecoding(CachedImageClient& client)
{
    ASSERT(client.resourceClientType() == CachedImageClient::expectedType());
    if (m_clientsWaitingForAsyncDecoding.contains(client))
        return;
    if (!m_clients.contains(client)) {
        // If the <html> element does not have its own background specified, painting the root box
        // renderer uses the style of the <body> element, see RenderView::rendererForRootBackground().
        // In this case, the client we are asked to add is the root box renderer. Since we can't add
        // a client to m_clientsWaitingForAsyncDecoding unless it is one of the m_clients, we are going
        // to cancel the repaint optimization we do in CachedImage::imageFrameAvailable() by adding
        // all the m_clients to m_clientsWaitingForAsyncDecoding.
        CachedResourceClientWalker<CachedImageClient> walker(*this);
        while (auto* client = walker.next())
            m_clientsWaitingForAsyncDecoding.add(*client);
    } else
        m_clientsWaitingForAsyncDecoding.add(client);
}
    
void CachedImage::removeAllClientsWaitingForAsyncDecoding()
{
    if (m_clientsWaitingForAsyncDecoding.isEmptyIgnoringNullReferences() || !hasImage())
        return;

    RefPtr bitmapImage = dynamicDowncast<BitmapImage>(image());
    if (!bitmapImage)
        return;
    bitmapImage->stopDecodingWorkQueue();

    for (auto& client : m_clientsWaitingForAsyncDecoding)
        client.imageChanged(this);
    m_clientsWaitingForAsyncDecoding.clear();
}

void CachedImage::switchClientsToRevalidatedResource()
{
    ASSERT(is<CachedImage>(resourceToRevalidate()));
    // Pending container size requests need to be transferred to the revalidated resource.
    if (!m_pendingContainerContextRequests.isEmpty()) {
        // A copy of pending size requests is needed as they are deleted during CachedResource::switchClientsToRevalidateResouce().
        ContainerContextRequests switchContainerContextRequests;
        for (auto& request : m_pendingContainerContextRequests)
            switchContainerContextRequests.set(request.key, request.value);
        CachedResource::switchClientsToRevalidatedResource();
        CachedResourceHandle revalidatedCachedImage = downcast<CachedImage>(*resourceToRevalidate());
        for (auto& request : switchContainerContextRequests)
            revalidatedCachedImage->setContainerContextForClient(request.key, request.value.containerSize, request.value.containerZoom, request.value.imageURL);
        return;
    }

    CachedResource::switchClientsToRevalidatedResource();
}

void CachedImage::allClientsRemoved()
{
    m_pendingContainerContextRequests.clear();
    m_clientsWaitingForAsyncDecoding.clear();
    if (RefPtr image = m_image; image && !errorOccurred())
        image->resetAnimation();
}

std::pair<WeakPtr<Image>, float> CachedImage::brokenImage(float deviceScaleFactor) const
{
    if (deviceScaleFactor >= 3) {
        static NeverDestroyed<Image*> brokenImageVeryHiRes(&ImageAdapter::loadPlatformResource("missingImage@3x").leakRef());
        return std::make_pair(WeakPtr { *brokenImageVeryHiRes }, 3);
    }

    if (deviceScaleFactor >= 2) {
        static NeverDestroyed<Image*> brokenImageHiRes(&ImageAdapter::loadPlatformResource("missingImage@2x").leakRef());
        return std::make_pair(WeakPtr { *brokenImageHiRes }, 2);
    }

    static NeverDestroyed<Image*> brokenImageLoRes(&ImageAdapter::loadPlatformResource("missingImage").leakRef());
    return std::make_pair(WeakPtr { *brokenImageLoRes }, 1);
}

bool CachedImage::willPaintBrokenImage() const
{
    return errorOccurred() && m_shouldPaintBrokenImage;
}

Image* CachedImage::image() const
{
    if (errorOccurred() && m_shouldPaintBrokenImage) {
        // Returning the 1x broken image is non-ideal, but we cannot reliably access the appropriate
        // deviceScaleFactor from here. It is critical that callers use CachedImage::brokenImage() 
        // when they need the real, deviceScaleFactor-appropriate broken image icon. 
        return brokenImage(1).first.get();
    }

    if (m_image)
        return m_image.get();

    return &Image::nullImage();
}

RefPtr<Image> CachedImage::protectedImage() const
{
    return image();
}

Image* CachedImage::imageForRenderer(const RenderObject* renderer)
{
    if (errorOccurred() && m_shouldPaintBrokenImage) {
        // Returning the 1x broken image is non-ideal, but we cannot reliably access the appropriate
        // deviceScaleFactor from here. It is critical that callers use CachedImage::brokenImage() 
        // when they need the real, deviceScaleFactor-appropriate broken image icon. 
        return brokenImage(1).first.get();
    }

    if (!m_image)
        return &Image::nullImage();

    if (m_image->drawsSVGImage()) {
        RefPtr image = m_svgImageCache->imageForRenderer(renderer);
        if (image != &Image::nullImage())
            return image.get();
    }
    return m_image.get();
}

void CachedImage::setContainerContextForClient(const CachedImageClient& client, const LayoutSize& containerSize, float containerZoom, const URL& imageURL)
{
    if (containerSize.isEmpty())
        return;
    ASSERT(containerZoom);
    RefPtr image = m_image;
    if (!image) {
        m_pendingContainerContextRequests.set(client, ContainerContext { containerSize, containerZoom, imageURL });
        return;
    }

    if (!image->drawsSVGImage()) {
        image->setContainerSize(containerSize);
        return;
    }

    m_svgImageCache->setContainerContextForClient(client, containerSize, containerZoom, imageURL);
}

FloatSize CachedImage::imageSizeForRenderer(const RenderElement* renderer, SizeType sizeType) const
{
    RefPtr image = m_image;
    if (!image)
        return { };

#if ENABLE(MULTI_REPRESENTATION_HEIC)
    if (CheckedPtr renderImage = dynamicDowncast<RenderImage>(renderer); renderImage && renderImage->isMultiRepresentationHEIC()) {
        auto metrics = renderImage->style().fontCascade().primaryFont()->metricsForMultiRepresentationHEIC();
        return metrics.size();
    }
#endif

    if (image->drawsSVGImage() && sizeType == UsedSize)
        return m_svgImageCache->imageSizeForRenderer(renderer);

    return image->size(renderer ? renderer->imageOrientation() : ImageOrientation(ImageOrientation::Orientation::FromImage));
}


LayoutSize CachedImage::unclampedImageSizeForRenderer(const RenderElement* renderer, float multiplier, SizeType sizeType) const
{
    LayoutSize imageSize = LayoutSize(imageSizeForRenderer(renderer, sizeType));
    if (imageSize.isEmpty() || multiplier == 1.0f)
        return imageSize;

    float widthScale = m_image->hasRelativeWidth() ? 1.0f : multiplier;
    float heightScale = m_image->hasRelativeHeight() ? 1.0f : multiplier;
    imageSize.scale(widthScale, heightScale);
    return imageSize;    
}

LayoutSize CachedImage::imageSizeForRenderer(const RenderElement* renderer, float multiplier, SizeType sizeType) const
{
    auto imageSize = unclampedImageSizeForRenderer(renderer, multiplier, sizeType);
    if (imageSize.isEmpty() || multiplier == 1.0f)
        return imageSize;

    // Don't let images that have a width/height >= 1 shrink below 1 when zoomed.
    LayoutSize minimumSize(imageSize.width() > 0 ? 1 : 0, imageSize.height() > 0 ? 1 : 0);
    imageSize.clampToMinimumSize(minimumSize);

    ASSERT(multiplier != 1.0f || (imageSize.width().fraction() == 0.0f && imageSize.height().fraction() == 0.0f));
    return imageSize;
}

void CachedImage::computeIntrinsicDimensions(Length& intrinsicWidth, Length& intrinsicHeight, FloatSize& intrinsicRatio)
{
    if (RefPtr image = m_image)
        image->computeIntrinsicDimensions(intrinsicWidth, intrinsicHeight, intrinsicRatio);
}

bool CachedImage::hasHDRContent() const
{
    return m_image && m_image->hasHDRContent();
}

void CachedImage::notifyObservers(const IntRect* changeRect)
{
    CachedResourceClientWalker<CachedImageClient> walker(*this);
    while (CachedImageClient* c = walker.next())
        c->imageChanged(this, changeRect);
}

void CachedImage::checkShouldPaintBrokenImage()
{
    if (!m_loader || m_loader->reachedTerminalState() || !m_loader->frameLoader())
        return;

    m_shouldPaintBrokenImage = m_loader->frameLoader()->client().shouldPaintBrokenImage(url());
}

void CachedImage::clear()
{
    destroyDecodedData();
    clearImage();
    m_pendingContainerContextRequests.clear();
    m_clientsWaitingForAsyncDecoding.clear();
    setEncodedSize(0);
}

inline void CachedImage::createImage()
{
    // Create the image if it doesn't yet exist.
    if (m_image)
        return;

    m_imageObserver = CachedImageObserver::create(*this);

    m_image = Image::create(*m_imageObserver);

    if (RefPtr image = m_image) {
        if (auto* svgImage = dynamicDowncast<SVGImage>(*image))
            m_svgImageCache = makeUnique<SVGImageCache>(svgImage);

        // Send queued container size requests.
        if (image->usesContainerSize()) {
            for (auto& request : m_pendingContainerContextRequests)
                setContainerContextForClient(request.key, request.value.containerSize, request.value.containerZoom, request.value.imageURL);
        }
        m_pendingContainerContextRequests.clear();
        m_clientsWaitingForAsyncDecoding.clear();
    }
}

CachedImage::CachedImageObserver::CachedImageObserver(CachedImage& image)
{
    m_cachedImages.add(image);
}

void CachedImage::CachedImageObserver::encodedDataStatusChanged(const Image& image, EncodedDataStatus status)
{
    for (CachedResourceHandle cachedImage : m_cachedImages)
        cachedImage->encodedDataStatusChanged(image, status);
}

void CachedImage::CachedImageObserver::decodedSizeChanged(const Image& image, long long delta)
{
    for (CachedResourceHandle cachedImage : m_cachedImages)
        cachedImage->decodedSizeChanged(image, delta);
}

void CachedImage::CachedImageObserver::didDraw(const Image& image)
{
    for (CachedResourceHandle cachedImage : m_cachedImages)
        cachedImage->didDraw(image);
}

bool CachedImage::CachedImageObserver::canDestroyDecodedData(const Image& image) const
{
    for (CachedResourceHandle cachedImage : m_cachedImages) {
        if (&image != cachedImage->image())
            continue;
        if (!cachedImage->canDestroyDecodedData(image))
            return false;
    }
    return true;
}

void CachedImage::CachedImageObserver::imageFrameAvailable(const Image& image, ImageAnimatingState animatingState, const IntRect* changeRect, DecodingStatus decodingStatus)
{
    for (CachedResourceHandle cachedImage : m_cachedImages)
        cachedImage->imageFrameAvailable(image, animatingState, changeRect, decodingStatus);
}

void CachedImage::CachedImageObserver::changedInRect(const Image& image, const IntRect* rect)
{
    for (CachedResourceHandle cachedImage : m_cachedImages)
        cachedImage->changedInRect(image, rect);
}

void CachedImage::CachedImageObserver::imageContentChanged(const Image& image)
{
    for (CachedResourceHandle cachedImage : m_cachedImages)
        cachedImage->imageContentChanged(image);
}

void CachedImage::CachedImageObserver::scheduleRenderingUpdate(const Image& image)
{
    for (CachedResourceHandle cachedImage : m_cachedImages)
        cachedImage->scheduleRenderingUpdate(image);
}

bool CachedImage::CachedImageObserver::allowsAnimation(const Image& image) const
{
    // *::allowsAnimation can only return false when systemAllowsAnimationControls == true,
    // so this prevents unnecessary work by exiting early.
    if (!Image::systemAllowsAnimationControls())
        return true;

    for (CachedResourceHandle cachedImage : m_cachedImages) {
        if (cachedImage->allowsAnimation(image))
            return true;
    }
    return false;
}

inline void CachedImage::clearImage()
{
    if (!m_image)
        return;

    if (RefPtr imageObserver = std::exchange(m_imageObserver, nullptr)) {
        imageObserver->cachedImages().remove(*this);

        if (imageObserver->cachedImages().isEmptyIgnoringNullReferences()) {
            ASSERT(imageObserver->hasOneRef());
            protectedImage()->setImageObserver(nullptr);
        }
    }

    m_image = nullptr;
    m_lastUpdateImageDataTime = { };
    m_updateImageDataCount = 0;
    m_allowsOrientationOverride = true;
}

void CachedImage::updateBufferInternal(const FragmentedSharedBuffer& data)
{
    CachedResourceHandle protectedThis { *this };
    m_data = const_cast<FragmentedSharedBuffer*>(&data);
    setEncodedSize(m_data->size());
    createImage();

    // Don't update the image with the new buffer very often. Changing the decoder
    // internal data and repainting the observers sometimes are very expensive operations.
    if (!m_forceUpdateImageDataEnabledForTesting && shouldDeferUpdateImageData())
        return;

    EncodedDataStatus encodedDataStatus = EncodedDataStatus::Unknown;

    // Have the image update its data from its internal buffer. Decoding the image data
    // will be delayed until info (like size or specific image frames) are queried which
    // usually happens when the observers are repainted.
    encodedDataStatus = updateImageData(false);

    if (encodedDataStatus > EncodedDataStatus::Error && encodedDataStatus < EncodedDataStatus::SizeAvailable)
        return;

    if (encodedDataStatus == EncodedDataStatus::Error || m_image->isNull()) {
        // Image decoding failed. Either we need more image data or the image data is malformed.
        error(errorOccurred() ? status() : DecodeError);
        if (inCache())
            MemoryCache::singleton().remove(*this);
        if (RefPtr loader = m_loader; loader && encodedDataStatus == EncodedDataStatus::Error)
            loader->cancel();
        return;
    }

    // Tell our observers to try to draw.
    notifyObservers();
}

bool CachedImage::shouldDeferUpdateImageData() const
{
    static constexpr std::array<double, 5> updateImageDataBackoffIntervals { 0, 1, 3, 6, 15 };
    unsigned interval = m_updateImageDataCount;

    // The first time through, the chunk time will be 0 and the image will get an update.
    return (MonotonicTime::now() - m_lastUpdateImageDataTime).seconds() < updateImageDataBackoffIntervals[interval];
}

RefPtr<SharedBuffer> CachedImage::convertedDataIfNeeded(const FragmentedSharedBuffer* data) const
{
    if (!data)
        return nullptr;
    return data->makeContiguous();
}

void CachedImage::didUpdateImageData()
{
    m_lastUpdateImageDataTime = MonotonicTime::now();
    unsigned previous = m_updateImageDataCount;
    if (previous != maxUpdateImageDataCount)
        m_updateImageDataCount += 1;
}

EncodedDataStatus CachedImage::updateImageData(bool allDataReceived)
{
    RefPtr image = m_image;
    if (!image || !m_data)
        return EncodedDataStatus::Error;
    EncodedDataStatus result = image->setData(m_data.copyRef(), allDataReceived);
    didUpdateImageData();
    return result;
}

void CachedImage::updateBuffer(const FragmentedSharedBuffer& buffer)
{
    ASSERT(dataBufferingPolicy() == DataBufferingPolicy::BufferData);
    updateBufferInternal(buffer);
}

void CachedImage::updateData(const SharedBuffer& data)
{
    ASSERT(dataBufferingPolicy() == DataBufferingPolicy::DoNotBufferData);
    updateBufferInternal(data);
}

void CachedImage::finishLoading(const FragmentedSharedBuffer* data, const NetworkLoadMetrics& metrics)
{
    m_data = convertedDataIfNeeded(data);
    if (m_data) {
        setEncodedSize(m_data->size());
        createImage();
    }

    EncodedDataStatus encodedDataStatus = updateImageData(true);

    if (encodedDataStatus == EncodedDataStatus::Error || m_image->isNull()) {
        // Image decoding failed; the image data is malformed.
        error(errorOccurred() ? status() : DecodeError);
        if (inCache())
            MemoryCache::singleton().remove(*this);
        return;
    }

    setLoading(false);
    setAllowsOrientationOverride(isCORSSameOrigin() || m_image->sourceURL().protocolIsData());

    notifyObservers();
    CachedResource::finishLoading(data, metrics);
}

void CachedImage::didReplaceSharedBufferContents()
{
    if (RefPtr image = m_image) {
        // Let the Image know that the FragmentedSharedBuffer has been rejigged, so it can let go of any references to the heap-allocated resource buffer.
        // FIXME(rdar://problem/24275617): It would be better if we could somehow tell the Image's decoder to swap in the new contents without destroying anything.
        image->destroyDecodedData(true);
    }
    CachedResource::didReplaceSharedBufferContents();
}

void CachedImage::error(CachedResource::Status status)
{
    checkShouldPaintBrokenImage();
    clear();
    CachedResource::error(status);
    notifyObservers();
}

void CachedImage::responseReceived(ResourceResponse&& newResponse)
{
    if (!response().isNull())
        clear();
    CachedResource::responseReceived(WTFMove(newResponse));
}

void CachedImage::destroyDecodedData()
{
    bool canDeleteImage = !m_image || (m_image->hasOneRef() && m_image->isBitmapImage());
    if (canDeleteImage && !isLoading() && !hasClients()) {
        m_image = nullptr;
        setDecodedSize(0);
    } else if (RefPtr image = m_image; image && !errorOccurred())
        image->destroyDecodedData();
}

void CachedImage::encodedDataStatusChanged(const Image& image, EncodedDataStatus)
{
    if (&image != m_image)
        return;

    notifyObservers();
}

void CachedImage::decodedSizeChanged(const Image& image, long long delta)
{
    if (&image != m_image)
        return;

    ASSERT(delta >= 0 || decodedSize() + delta >= 0);
    setDecodedSize(static_cast<unsigned>(decodedSize() + delta));
}

void CachedImage::didDraw(const Image& image)
{
    if (&image != m_image)
        return;
    
    MonotonicTime timeStamp = LocalFrameView::currentPaintTimeStamp();
    if (!timeStamp) // If didDraw is called outside of a Frame paint.
        timeStamp = MonotonicTime::now();
    
    CachedResource::didAccessDecodedData(timeStamp);
}

bool CachedImage::canDestroyDecodedData(const Image& image) const
{
    if (&image != m_image)
        return false;

    CachedResourceClientWalker<CachedImageClient> walker(*this);
    while (CachedImageClient* client = walker.next()) {
        if (!client->canDestroyDecodedData())
            return false;
    }

    return true;
}

void CachedImage::imageFrameAvailable(const Image& image, ImageAnimatingState animatingState, const IntRect* changeRect, DecodingStatus decodingStatus)
{
    if (&image != m_image)
        return;

    CachedResourceClientWalker<CachedImageClient> walker(*this);
    VisibleInViewportState visibleState = VisibleInViewportState::No;

    while (CachedImageClient* client = walker.next()) {
        // All the clients of animated images have to be notified. The new frame has to be drawn in all of them.
        if (animatingState == ImageAnimatingState::No && !m_clientsWaitingForAsyncDecoding.contains(*client))
            continue;
        if (client->imageFrameAvailable(*this, animatingState, changeRect) == VisibleInViewportState::Yes)
            visibleState = VisibleInViewportState::Yes;
    }

    if (visibleState == VisibleInViewportState::No && animatingState == ImageAnimatingState::Yes)
        protectedImage()->stopAnimation();

    if (decodingStatus != DecodingStatus::Partial)
        m_clientsWaitingForAsyncDecoding.clear();
}

void CachedImage::changedInRect(const Image& image, const IntRect* rect)
{
    if (&image != m_image)
        return;
    notifyObservers(rect);
}

void CachedImage::imageContentChanged(const Image& image)
{
    if (&image != m_image)
        return;

    CachedResourceClientWalker<CachedImageClient> walker(*this);
    while (auto* client = walker.next())
        client->imageContentChanged(*this);
}

void CachedImage::scheduleRenderingUpdate(const Image& image)
{
    if (&image != m_image)
        return;

    CachedResourceClientWalker<CachedImageClient> walker(*this);
    while (auto* client = walker.next())
        client->scheduleRenderingUpdateForImage(*this);
}

bool CachedImage::allowsAnimation(const Image& image) const
{
    if (&image != m_image)
        return false;

    if (!Image::systemAllowsAnimationControls())
        return true;

    CachedResourceClientWalker<CachedImageClient> walker(*this);
    while (auto* client = walker.next()) {
        if (!client->allowsAnimation())
            return false;
    }
    return true;
}

bool CachedImage::currentFrameKnownToBeOpaque(const RenderElement* renderer)
{
    RefPtr image = imageForRenderer(renderer);
    return image->currentFrameKnownToBeOpaque();
}

bool CachedImage::isOriginClean(SecurityOrigin* origin)
{
    ASSERT_UNUSED(origin, origin);
    ASSERT(this->origin());
    ASSERT(origin->toString() == this->origin()->toString());
    return !loadFailedOrCanceled() && isCORSSameOrigin();
}

CachedResource::RevalidationDecision CachedImage::makeRevalidationDecision(CachePolicy cachePolicy) const
{
    if (isManuallyCached()) [[unlikely]] {
        // Do not revalidate manually cached images. This mechanism is used as a
        // way to efficiently share an image from the client to content and
        // the URL for that image may not represent a resource that can be
        // retrieved by standard means. If the manual caching SPI is used, it is
        // incumbent on the client to only use valid resources.
        return RevalidationDecision::No;
    }
    return CachedResource::makeRevalidationDecision(cachePolicy);
}

bool CachedImage::canSkipRevalidation(const CachedResourceLoader& loader, const CachedResourceRequest& request) const
{
    if (options().mode != request.options().mode || options().credentials != request.options().credentials || resourceRequest().allowCookies() != request.resourceRequest().allowCookies())
        return false;

    // Skip revalidation as per https://html.spec.whatwg.org/#ignore-higher-layer-caching which defines a per-document image list.
    // This rule is loosely implemented by other browsers, we could relax it and should update it once memory cache is properly specified.
    return m_skippingRevalidationDocument && loader.document() == m_skippingRevalidationDocument;
}

bool CachedImage::isVisibleInViewport(const Document& document) const
{
    CachedResourceClientWalker<CachedImageClient> walker(*this);
    while (auto* client = walker.next()) {
        if (client->imageVisibleInViewport(document) == VisibleInViewportState::Yes)
            return true;
    }
    return false;
}

} // namespace WebCore
