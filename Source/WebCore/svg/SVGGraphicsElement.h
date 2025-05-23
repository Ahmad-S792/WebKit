/*
 * Copyright (C) 2004, 2005, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007 Rob Buis <buis@kde.org>
 * Copyright (C) 2018-2019 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
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

#pragma once

#include "SVGAnimatedPropertyImpl.h"
#include "SVGElement.h"
#include "SVGTests.h"
#include "SVGTransformList.h"
#include "SVGTransformable.h"
#include <wtf/TZoneMalloc.h>

namespace WebCore {

class AffineTransform;
class Path;
class SVGRect;
class SVGMatrix;

class SVGGraphicsElement : public SVGElement, public SVGTransformable, public SVGTests {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(SVGGraphicsElement);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(SVGGraphicsElement);
public:
    virtual ~SVGGraphicsElement();

    Ref<SVGMatrix> getCTMForBindings();
    AffineTransform getCTM(StyleUpdateStrategy = AllowStyleUpdate) override;

    Ref<SVGMatrix> getScreenCTMForBindings();
    AffineTransform getScreenCTM(StyleUpdateStrategy = AllowStyleUpdate) override;

    SVGElement* nearestViewportElement() const override;
    SVGElement* farthestViewportElement() const override;

    AffineTransform localCoordinateSpaceTransform(CTMScope mode) const override { return SVGTransformable::localCoordinateSpaceTransform(mode); }
    AffineTransform animatedLocalTransform() const override;
    AffineTransform* ensureSupplementalTransform() override;
    AffineTransform* supplementalTransform() const override { return m_supplementalTransform.get(); }

    virtual bool hasTransformRelatedAttributes() const { return !transform().concatenate().isIdentity() || m_supplementalTransform; }

    Ref<SVGRect> getBBoxForBindings();
    FloatRect getBBox(StyleUpdateStrategy = AllowStyleUpdate) override;

    bool shouldIsolateBlending() const { return m_shouldIsolateBlending; }
    void setShouldIsolateBlending(bool isolate) { m_shouldIsolateBlending = isolate; }

    // "base class" methods for all the elements which render as paths
    virtual Path toClipPath();
    RenderPtr<RenderElement> createElementRenderer(RenderStyle&&, const RenderTreePosition&) override;

    size_t approximateMemoryCost() const override { return sizeof(*this); }

    using PropertyRegistry = SVGPropertyOwnerRegistry<SVGGraphicsElement, SVGElement, SVGTests>;

    const SVGTransformList& transform() const { return m_transform->currentValue(); }
    Ref<const SVGTransformList> protectedTransform() const;
    SVGAnimatedTransformList& transformAnimated() { return m_transform; }

protected:
    SVGGraphicsElement(const QualifiedName&, Document&, UniqueRef<SVGPropertyRegistry>&&, OptionSet<TypeFlag> = { });

    void attributeChanged(const QualifiedName&, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason) override;
    void svgAttributeChanged(const QualifiedName&) override;
    void didAttachRenderers() override;

    void invalidateResourceImageBuffersIfNeeded();

private:
    bool isSVGGraphicsElement() const override { return true; }

    // Used by <animateMotion>
    std::unique_ptr<AffineTransform> m_supplementalTransform;

    // Used to isolate blend operations caused by masking.
    bool m_shouldIsolateBlending { false };

    Ref<SVGAnimatedTransformList> m_transform;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::SVGGraphicsElement)
    static bool isType(const WebCore::SVGElement& element) { return element.isSVGGraphicsElement(); }
    static bool isType(const WebCore::Node& node)
    {
        auto* svgElement = dynamicDowncast<WebCore::SVGElement>(node);
        return svgElement && isType(*svgElement);
    }
SPECIALIZE_TYPE_TRAITS_END()
