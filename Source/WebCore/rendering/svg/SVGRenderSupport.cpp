/*
 * Copyright (C) 2007, 2008 Rob Buis <buis@kde.org>
 * Copyright (C) 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2007 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009-2016 Google, Inc. All rights reserved.
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2009-2010. All rights reserved.
 * Copyright (C) 2018 Adobe Systems Incorporated. All rights reserved.
 * Copyright (C) 2020-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2025 Samuel Weinig <sam@webkit.org>
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

#include "config.h"
#include "SVGRenderSupport.h"

#include "ElementAncestorIteratorInlines.h"
#include "LegacyRenderSVGForeignObject.h"
#include "LegacyRenderSVGImage.h"
#include "LegacyRenderSVGResourceClipper.h"
#include "LegacyRenderSVGResourceFilter.h"
#include "LegacyRenderSVGResourceMarker.h"
#include "LegacyRenderSVGResourceMasker.h"
#include "LegacyRenderSVGRoot.h"
#include "LegacyRenderSVGShapeInlines.h"
#include "LegacyRenderSVGTransformableContainer.h"
#include "LegacyRenderSVGViewportContainer.h"
#include "NodeRenderStyle.h"
#include "PathOperation.h"
#include "ReferencedSVGResources.h"
#include "RenderChildIterator.h"
#include "RenderElement.h"
#include "RenderElementInlines.h"
#include "RenderGeometryMap.h"
#include "RenderIterator.h"
#include "RenderLayer.h"
#include "RenderObjectInlines.h"
#include "RenderSVGResourceClipper.h"
#include "RenderSVGRoot.h"
#include "RenderSVGShapeInlines.h"
#include "RenderSVGText.h"
#include "SVGClipPathElement.h"
#include "SVGElementTypeHelpers.h"
#include "SVGGeometryElement.h"
#include "SVGRenderStyle.h"
#include "SVGResources.h"
#include "SVGResourcesCache.h"
#include "TransformOperationData.h"
#include "TransformState.h"
#include <numbers>

namespace WebCore {

LayoutRect SVGRenderSupport::clippedOverflowRectForRepaint(const RenderElement& renderer, const RenderLayerModelObject* repaintContainer, RenderObject::VisibleRectContext context)
{
    // Return early for any cases where we don't actually paint
    if (renderer.isInsideEntirelyHiddenLayer())
        return { };

    // Pass our local paint rect to computeFloatVisibleRectInContainer() which will
    // map to parent coords and recurse up the parent chain.
    return enclosingLayoutRect(renderer.computeFloatRectForRepaint(renderer.repaintRectInLocalCoordinates(context.repaintRectCalculation()), repaintContainer));
}

std::optional<FloatRect> SVGRenderSupport::computeFloatVisibleRectInContainer(const RenderElement& renderer, const FloatRect& rect, const RenderLayerModelObject* container, RenderObject::VisibleRectContext context)
{
    // Ensure our parent is an SVG object.
    ASSERT(renderer.parent());
    auto& parent = *renderer.parent();
    if (!is<SVGElement>(parent.element()))
        return FloatRect();

    FloatRect adjustedRect = rect;
    adjustedRect.inflate(Style::evaluate(renderer.style().outlineWidth()));

    // Translate to coords in our parent renderer, and then call computeFloatVisibleRectInContainer() on our parent.
    adjustedRect = renderer.localToParentTransform().mapRect(adjustedRect);

    return parent.computeFloatVisibleRectInContainer(adjustedRect, container, context);
}

const RenderElement& SVGRenderSupport::localToParentTransform(const RenderElement& renderer, AffineTransform &transform)
{
    ASSERT(renderer.parent());
    auto& parent = *renderer.parent();

    // At the SVG/HTML boundary (aka LegacyRenderSVGRoot), we apply the localToBorderBoxTransform
    // to map an element from SVG viewport coordinates to CSS box coordinates.
    if (auto* svgRoot = dynamicDowncast<LegacyRenderSVGRoot>(parent))
        transform = svgRoot->localToBorderBoxTransform() * renderer.localToParentTransform();
    else
        transform = renderer.localToParentTransform();

    return parent;
}

void SVGRenderSupport::mapLocalToContainer(const RenderElement& renderer, const RenderLayerModelObject* ancestorContainer, TransformState& transformState, bool* wasFixed)
{
    AffineTransform transform;
    auto& parent = localToParentTransform(renderer, transform);

    transformState.applyTransform(transform);

    OptionSet<MapCoordinatesMode> mode = UseTransforms;
    parent.mapLocalToContainer(ancestorContainer, transformState, mode, wasFixed);
}

const RenderElement* SVGRenderSupport::pushMappingToContainer(const RenderElement& renderer, const RenderLayerModelObject* ancestorToStopAt, RenderGeometryMap& geometryMap)
{
    ASSERT_UNUSED(ancestorToStopAt, ancestorToStopAt != &renderer);

    AffineTransform transform;
    auto& parent = localToParentTransform(renderer, transform);

    geometryMap.push(&renderer, transform);
    return &parent;
}

LayoutRepainter::CheckForRepaint SVGRenderSupport::checkForSVGRepaintDuringLayout(const RenderElement& renderer)
{
    if (!renderer.checkForRepaintDuringLayout())
        return LayoutRepainter::CheckForRepaint::No;
    // When a parent container is transformed in SVG, all children will be painted automatically
    // so we are able to skip redundant repaint checks.
    if (CheckedPtr parent = dynamicDowncast<LegacyRenderSVGContainer>(renderer.parent())) {
        if (parent->isRepaintSuspendedForChildren() || parent->didTransformToRootUpdate())
            return LayoutRepainter::CheckForRepaint::No;
    }
    return LayoutRepainter::CheckForRepaint::Yes;
}

// https://svgwg.org/svg2-draft/coords.html#BoundingBoxes
static bool hasValidBoundingBoxForContainer(const RenderObject& object)
{
    if (auto* shape = dynamicDowncast<LegacyRenderSVGShape>(object))
        return !shape->isRenderingDisabled();

    if (auto* text = dynamicDowncast<RenderSVGText>(object))
        return text->isObjectBoundingBoxValid();

    if (auto* container = dynamicDowncast<LegacyRenderSVGContainer>(object))
        return !container->isLegacyRenderSVGHiddenContainer();

    if (auto* foreignObject = dynamicDowncast<LegacyRenderSVGForeignObject>(object))
        return foreignObject->isObjectBoundingBoxValid();

    if (auto* image = dynamicDowncast<LegacyRenderSVGImage>(object))
        return image->isObjectBoundingBoxValid();

    return false;
}

auto SVGRenderSupport::computeContainerBoundingBoxes(const RenderElement& container, RepaintRectCalculation repaintRectCalculation) -> ContainerBoundingBoxes
{
    ContainerBoundingBoxes result;

    for (CheckedRef current : childrenOfType<RenderObject>(container)) {
        if (!hasValidBoundingBoxForContainer(current))
            continue;

        auto& transform = current->localToParentTransform();

        auto repaintRect = current->repaintRectInLocalCoordinates(repaintRectCalculation);
        if (!transform.isIdentity())
            repaintRect = transform.mapRect(repaintRect);

        result.repaintBoundingBox.unite(repaintRect);

        if (auto* container = dynamicDowncast<LegacyRenderSVGContainer>(current.ptr()); (container && !container->isObjectBoundingBoxValid()))
            continue;

        auto objectBounds = current->objectBoundingBox();
        if (!transform.isIdentity())
            objectBounds = transform.mapRect(objectBounds);

        if (!result.objectBoundingBox)
            result.objectBoundingBox = objectBounds;
        else
            result.objectBoundingBox->uniteEvenIfEmpty(objectBounds);
    }

    return result;
}

FloatRect SVGRenderSupport::computeContainerStrokeBoundingBox(const RenderElement& container)
{
    ASSERT(container.isLegacyRenderSVGRoot() || container.isLegacyRenderSVGContainer());
    FloatRect strokeBoundingBox = FloatRect();
    for (CheckedRef current : childrenOfType<RenderObject>(container)) {
        if (current->isLegacyRenderSVGHiddenContainer())
            continue;

        // Don't include elements in the union that do not render.
        if (auto* shape = dynamicDowncast<LegacyRenderSVGShape>(current.ptr()); shape && shape->isRenderingDisabled())
            continue;

        FloatRect childStrokeBoundingBox = current->strokeBoundingBox();
        if (auto* currentElement = dynamicDowncast<RenderElement>(current.get()))
            SVGRenderSupport::intersectRepaintRectWithResources(*currentElement, childStrokeBoundingBox, RepaintRectCalculation::Accurate);
        const AffineTransform& transform = current->localToParentTransform();
        if (transform.isIdentity())
            strokeBoundingBox.unite(childStrokeBoundingBox);
        else
            strokeBoundingBox.unite(transform.mapRect(childStrokeBoundingBox));
    }
    return strokeBoundingBox;
}

bool SVGRenderSupport::paintInfoIntersectsRepaintRect(const FloatRect& localRepaintRect, const AffineTransform& localTransform, const PaintInfo& paintInfo)
{
    if (localTransform.isIdentity())
        return localRepaintRect.intersects(paintInfo.rect);

    return localTransform.mapRect(localRepaintRect).intersects(paintInfo.rect);
}

LegacyRenderSVGRoot* SVGRenderSupport::findTreeRootObject(RenderElement& start)
{
    return lineageOfType<LegacyRenderSVGRoot>(start).first();
}

const LegacyRenderSVGRoot* SVGRenderSupport::findTreeRootObject(const RenderElement& start)
{
    return lineageOfType<LegacyRenderSVGRoot>(start).first();
}

static inline void invalidateResourcesOfChildren(RenderElement& renderer)
{
    ASSERT(!renderer.needsLayout());
    if (auto* resources = SVGResourcesCache::cachedResourcesForRenderer(renderer))
        resources->removeClientFromCacheAndMarkForInvalidation(renderer, false);

    for (auto& child : childrenOfType<RenderElement>(renderer))
        invalidateResourcesOfChildren(child);
}

static inline bool layoutSizeOfNearestViewportChanged(const RenderElement& renderer)
{
    for (CheckedPtr start = &renderer; start; start = start->parent()) {
        if (auto* svgRoot = dynamicDowncast<LegacyRenderSVGRoot>(*start))
            return svgRoot->isLayoutSizeChanged();
        if (auto* container = dynamicDowncast<LegacyRenderSVGViewportContainer>(*start))
            return container->isLayoutSizeChanged();
    }
    ASSERT_NOT_REACHED();
    return false;
}

bool SVGRenderSupport::transformToRootChanged(RenderElement* ancestor)
{
    while (ancestor && !ancestor->isRenderOrLegacyRenderSVGRoot()) {
        if (CheckedPtr container = dynamicDowncast<LegacyRenderSVGTransformableContainer>(*ancestor))
            return container->didTransformToRootUpdate();
        if (CheckedPtr container = dynamicDowncast<LegacyRenderSVGViewportContainer>(*ancestor))
            return container->didTransformToRootUpdate();
        ancestor = ancestor->parent();
    }

    return false;
}

void SVGRenderSupport::layoutDifferentRootIfNeeded(const RenderElement& renderer)
{
    if (auto* resources = SVGResourcesCache::cachedResourcesForRenderer(renderer))
        resources->layoutDifferentRootIfNeeded(renderer);
}

void SVGRenderSupport::layoutChildren(RenderElement& start, bool selfNeedsLayout)
{
    bool layoutSizeChanged = layoutSizeOfNearestViewportChanged(start);
    bool transformChanged = transformToRootChanged(&start);
    SingleThreadWeakHashSet<RenderElement> elementsThatDidNotReceiveLayout;

    for (auto& child : childrenOfType<RenderObject>(start)) {
        bool needsLayout = selfNeedsLayout;
        bool childEverHadLayout = child.everHadLayout();

        if (transformChanged) {
            // If the transform changed we need to update the text metrics (note: this also happens for layoutSizeChanged=true).
            if (CheckedPtr text = dynamicDowncast<RenderSVGText>(child))
                text->setNeedsTextMetricsUpdate();
            needsLayout = true;
        }

        if (layoutSizeChanged) {
            // When selfNeedsLayout is false and the layout size changed, we have to check whether this child uses relative lengths
            if (RefPtr element = dynamicDowncast<SVGElement>(child.node()); element && element->hasRelativeLengths()) {
                // When the layout size changed and when using relative values tell the LegacyRenderSVGShape to update its shape object
                if (CheckedPtr shape = dynamicDowncast<LegacyRenderSVGShape>(child))
                    shape->setNeedsShapeUpdate();
                else if (CheckedPtr svgText = dynamicDowncast<RenderSVGText>(child)) {
                    svgText->setNeedsTextMetricsUpdate();
                    svgText->setNeedsPositioningValuesUpdate();
                }
                child.setNeedsTransformUpdate();
                needsLayout = true;
            }
        }

        if (needsLayout)
            child.setNeedsLayout(MarkOnlyThis);

        if (child.needsLayout()) {
            CheckedRef childElement = downcast<RenderElement>(child);
            layoutDifferentRootIfNeeded(childElement);
            childElement->layout();
            // Renderers are responsible for repainting themselves when changing, except
            // for the initial paint to avoid potential double-painting caused by non-sensical "old" bounds.
            // We could handle this in the individual objects, but for now it's easier to have
            // parent containers call repaint().  (RenderBlock::layout* has similar logic.)
            if (!childEverHadLayout)
                child.repaint();
        } else if (layoutSizeChanged) {
            if (CheckedPtr childElement = dynamicDowncast<RenderElement>(child))
                elementsThatDidNotReceiveLayout.add(*childElement);
        }

        ASSERT(!child.needsLayout());
    }

    if (!layoutSizeChanged) {
        ASSERT(elementsThatDidNotReceiveLayout.isEmptyIgnoringNullReferences());
        return;
    }

    // If the layout size changed, invalidate all resources of all children that didn't go through the layout() code path.
    for (auto& element : elementsThatDidNotReceiveLayout)
        invalidateResourcesOfChildren(element);
}

bool SVGRenderSupport::isOverflowHidden(const RenderElement& renderer)
{
    // LegacyRenderSVGRoot should never query for overflow state - it should always clip itself to the initial viewport size.
    ASSERT(!renderer.isDocumentElementRenderer());

    return isNonVisibleOverflow(renderer.style().overflowX());
}

void SVGRenderSupport::intersectRepaintRectWithResources(const RenderElement& renderer, FloatRect& repaintRect, RepaintRectCalculation repaintRectCalculation)
{
    auto* resources = SVGResourcesCache::cachedResourcesForRenderer(renderer);
    if (!resources)
        return;

    if (LegacyRenderSVGResourceFilter* filter = resources->filter())
        repaintRect = filter->resourceBoundingBox(renderer, repaintRectCalculation);

    if (LegacyRenderSVGResourceClipper* clipper = resources->clipper())
        repaintRect.intersect(clipper->resourceBoundingBox(renderer, repaintRectCalculation));

    if (auto* masker = resources->masker())
        repaintRect.intersect(masker->resourceBoundingBox(renderer, repaintRectCalculation));
}

bool SVGRenderSupport::filtersForceContainerLayout(const RenderElement& renderer)
{
    // If any of this container's children need to be laid out, and a filter is applied
    // to the container, we need to repaint the entire container.
    if (!renderer.normalChildNeedsLayout())
        return false;

    auto* resources = SVGResourcesCache::cachedResourcesForRenderer(renderer);
    if (!resources || !resources->filter())
        return false;

    return true;
}

inline FloatRect clipPathReferenceBox(const RenderElement& renderer, CSSBoxType boxType)
{
    FloatRect referenceBox;
    switch (boxType) {
    case CSSBoxType::BorderBox:
    case CSSBoxType::MarginBox:
    case CSSBoxType::StrokeBox:
    case CSSBoxType::BoxMissing:
        // FIXME: strokeBoundingBox() takes dasharray into account but shouldn't.
        referenceBox = renderer.strokeBoundingBox();
        break;
    case CSSBoxType::ViewBox:
        if (renderer.element()) {
            auto viewportSize = SVGLengthContext(downcast<SVGElement>(renderer.element())).viewportSize();
            if (viewportSize)
                referenceBox.setSize(*viewportSize);
            break;
        }
        [[fallthrough]];
    case CSSBoxType::ContentBox:
    case CSSBoxType::FillBox:
    case CSSBoxType::PaddingBox:
        referenceBox = renderer.objectBoundingBox();
        break;
    }
    return referenceBox;
}

inline bool isPointInCSSClippingArea(const RenderElement& renderer, const FloatPoint& point)
{
    return WTF::switchOn(renderer.style().clipPath(),
        [&](const Style::BasicShapePath& clipPath) {
            auto referenceBox = clipPathReferenceBox(renderer, clipPath.referenceBox());
            if (!referenceBox.contains(point))
                return false;
            return Style::path(clipPath.shape(), referenceBox).contains(point, Style::windRule(clipPath.shape()));
        },
        [&](const Style::BoxPath& clipPath) {
            auto referenceBox = clipPathReferenceBox(renderer, clipPath.referenceBox());
            if (!referenceBox.contains(point))
                return false;
            return FloatRoundedRect { referenceBox }.path().contains(point);
        },
        [&](const auto&) {
            return true;
        }
    );
}

void SVGRenderSupport::clipContextToCSSClippingArea(GraphicsContext& context, const RenderElement& renderer)
{
    WTF::switchOn(renderer.style().clipPath(),
        [&](const Style::BasicShapePath& clipPath) {
            auto localToParentTransform = renderer.localToParentTransform();

            auto referenceBox = clipPathReferenceBox(renderer, clipPath.referenceBox());
            referenceBox = localToParentTransform.mapRect(referenceBox);

            auto path = Style::path(clipPath.shape(), referenceBox);
            path.transform(valueOrDefault(localToParentTransform.inverse()));

            context.clipPath(path, Style::windRule(clipPath.shape()));
        },
        [&](const Style::BoxPath& clipPath) {
            auto referenceBox = clipPathReferenceBox(renderer, clipPath.referenceBox());
            context.clipPath(FloatRoundedRect { referenceBox }.path());
        },
        [&](const auto&) { }
    );
}

bool SVGRenderSupport::pointInClippingArea(const RenderElement& renderer, const FloatPoint& point)
{
    RELEASE_ASSERT(!renderer.document().settings().layerBasedSVGEngineEnabled());

    if (WTF::holdsAlternative<Style::BasicShapePath>(renderer.style().clipPath()) || WTF::holdsAlternative<Style::BoxPath>(renderer.style().clipPath()))
        return isPointInCSSClippingArea(renderer, point);

    // We just take clippers into account to determine if a point is on the node. The Specification may
    // change later and we also need to check maskers.
    auto* resources = SVGResourcesCache::cachedResourcesForRenderer(renderer);
    if (!resources)
        return true;

    if (LegacyRenderSVGResourceClipper* clipper = resources->clipper())
        return clipper->hitTestClipContent(renderer.objectBoundingBox(), point);

    return true;
}

void SVGRenderSupport::applyStrokeStyleToContext(GraphicsContext& context, const RenderStyle& style, const RenderElement& renderer)
{
    auto element = dynamicDowncast<SVGElement>(renderer.protectedElement());
    if (!element) {
        ASSERT_NOT_REACHED();
        return;
    }

    const SVGRenderStyle& svgStyle = style.svgStyle();

    SVGLengthContext lengthContext(element.get());
    context.setStrokeThickness(lengthContext.valueForLength(style.strokeWidth()));
    context.setLineCap(style.capStyle());
    context.setLineJoin(style.joinStyle());
    if (style.joinStyle() == LineJoin::Miter)
        context.setMiterLimit(style.strokeMiterLimit().value.value);

    auto& dashes = svgStyle.strokeDashArray();
    if (dashes.isNone())
        context.setStrokeStyle(StrokeStyle::SolidStroke);
    else {
        float scaleFactor = 1;

        if (auto geometryElement = dynamicDowncast<SVGGeometryElement>(*element)) {
            ASSERT(renderer.isRenderOrLegacyRenderSVGShape());
            // FIXME: A value of zero is valid. Need to differentiate this case from being unspecified.
            if (float pathLength = geometryElement->pathLength()) {
                if (CheckedPtr shape = dynamicDowncast<LegacyRenderSVGShape>(renderer))
                    scaleFactor = shape->getTotalLength() / pathLength;
                else if (CheckedPtr shape = dynamicDowncast<RenderSVGShape>(renderer))
                    scaleFactor = shape->getTotalLength() / pathLength;
            }
        }
        
        bool canSetLineDash = false;
        auto dashArray = DashArray::map(dashes, [&](auto& dash) -> DashArrayElement {
            auto value = lengthContext.valueForLength(dash) * scaleFactor;
            if (value > 0)
                canSetLineDash = true;
            return value;
        });

        if (canSetLineDash)
            context.setLineDash(dashArray, lengthContext.valueForLength(svgStyle.strokeDashOffset()) * scaleFactor);
        else
            context.setStrokeStyle(StrokeStyle::SolidStroke);
    }
}

void SVGRenderSupport::styleChanged(RenderElement& renderer, const RenderStyle* oldStyle)
{
    if (renderer.element() && renderer.element()->isSVGElement() && (!oldStyle || renderer.style().hasBlendMode() != oldStyle->hasBlendMode()))
        SVGRenderSupport::updateMaskedAncestorShouldIsolateBlending(renderer);
}

bool SVGRenderSupport::isolatesBlending(const RenderStyle& style)
{
    return style.hasPositionedMask() || style.hasFilter() || style.hasBlendMode() || !style.opacity().isOpaque();
}

void SVGRenderSupport::updateMaskedAncestorShouldIsolateBlending(const RenderElement& renderer)
{
    RefPtr element = renderer.element();
    ASSERT(element);
    ASSERT(element->isSVGElement());
    for (Ref ancestor : ancestorsOfType<SVGGraphicsElement>(*element)) {
        auto* style = ancestor->computedStyle();
        if (!style || !isolatesBlending(*style))
            continue;
        if (style->hasPositionedMask())
            ancestor->setShouldIsolateBlending(renderer.style().hasBlendMode());
        return;
    }
}

FloatRect SVGRenderSupport::calculateApproximateStrokeBoundingBox(const RenderElement& renderer)
{
    auto calculateApproximateScalingStrokeBoundingBox = [&]<typename Renderer>(const Renderer& renderer, FloatRect fillBoundingBox) -> FloatRect {
        // Implementation of
        // https://drafts.fxtf.org/css-masking/#compute-stroke-bounding-box
        // except that we ignore whether the stroke is none.

        ASSERT(renderer.style().svgStyle().hasStroke());

        auto strokeBoundingBox = fillBoundingBox;
        const float strokeWidth = renderer.strokeWidth();
        if (strokeWidth <= 0)
            return strokeBoundingBox;

        float delta = strokeWidth / 2;
        switch (renderer.shapeType()) {
        case Renderer::ShapeType::Empty: {
            // Spec: "A negative value is illegal. A value of zero disables rendering of the element."
            return strokeBoundingBox;
        }
        case Renderer::ShapeType::Ellipse:
        case Renderer::ShapeType::Circle:
            break;
        case Renderer::ShapeType::Rectangle:
        case Renderer::ShapeType::RoundedRectangle: {
#if USE(CG)
            // CoreGraphics can inflate the stroke by 1px when drawing a rectangle with antialiasing disabled at non-integer coordinates, we need to compensate.
            if (renderer.style().svgStyle().shapeRendering() == ShapeRendering::CrispEdges)
                delta += 1;
#endif
            break;
        }
        case Renderer::ShapeType::Path:
        case Renderer::ShapeType::Line: {
            auto& style = renderer.style();
            if (renderer.shapeType() == Renderer::ShapeType::Path && style.joinStyle() == LineJoin::Miter) {
                auto miter = style.strokeMiterLimit().value.value;
                if (miter < std::numbers::sqrt2 && style.capStyle() == LineCap::Square)
                    delta *= std::numbers::sqrt2;
                else
                    delta *= std::max(miter, 1.0f);
            } else if (style.capStyle() == LineCap::Square)
                delta *= std::numbers::sqrt2;
            break;
        }
        }

        strokeBoundingBox.inflate(delta);
        return strokeBoundingBox;
    };

    auto calculateApproximateNonScalingStrokeBoundingBox = [&](const auto& renderer, FloatRect fillBoundingBox) -> FloatRect {
        ASSERT(renderer.hasPath());
        ASSERT(renderer.style().svgStyle().hasStroke());
        ASSERT(renderer.hasNonScalingStroke());

        auto strokeBoundingBox = fillBoundingBox;
        auto nonScalingTransform = renderer.nonScalingStrokeTransform();
        if (auto inverse = nonScalingTransform.inverse()) {
            auto* usePath = renderer.nonScalingStrokePath(&renderer.path(), nonScalingTransform);
            auto strokeBoundingRect = calculateApproximateScalingStrokeBoundingBox(renderer, usePath->fastBoundingRect());
            strokeBoundingRect = inverse.value().mapRect(strokeBoundingRect);
            if (!strokeBoundingRect.isNaN())
                strokeBoundingBox.unite(strokeBoundingRect);
        }
        return strokeBoundingBox;
    };

    auto calculate = [&](const auto& renderer) {
        if (!renderer.style().svgStyle().hasStroke())
            return renderer.objectBoundingBox();
        if (renderer.hasNonScalingStroke())
            return calculateApproximateNonScalingStrokeBoundingBox(renderer, renderer.objectBoundingBox());
        return calculateApproximateScalingStrokeBoundingBox(renderer, renderer.objectBoundingBox());
    };

    if (CheckedPtr shape = dynamicDowncast<LegacyRenderSVGShape>(renderer))
        return shape->adjustStrokeBoundingBoxForMarkersAndZeroLengthLinecaps(RepaintRectCalculation::Fast, calculate(*shape));

    const auto& shape = downcast<RenderSVGShape>(renderer);
    return shape.adjustStrokeBoundingBoxForZeroLengthLinecaps(RepaintRectCalculation::Fast, calculate(shape));
}

}
