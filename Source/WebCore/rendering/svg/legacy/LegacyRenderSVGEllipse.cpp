/*
 * Copyright (C) 2012 Google, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY UNIVERSITY OF SZEGED ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL UNIVERSITY OF SZEGED OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "LegacyRenderSVGEllipse.h"

#include "LegacyRenderSVGShapeInlines.h"
#include "SVGCircleElement.h"
#include "SVGElementTypeHelpers.h"
#include "SVGEllipseElement.h"
#include "SVGRenderStyle.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(LegacyRenderSVGEllipse);

LegacyRenderSVGEllipse::LegacyRenderSVGEllipse(SVGGraphicsElement& element, RenderStyle&& style)
    : LegacyRenderSVGShape(Type::LegacySVGEllipse, element, WTFMove(style))
{
}

LegacyRenderSVGEllipse::~LegacyRenderSVGEllipse() = default;

void LegacyRenderSVGEllipse::updateShapeFromElement()
{
    // Before creating a new object we need to clear the cached bounding box
    // to avoid using garbage.
    clearPath();
    m_shapeType = ShapeType::Empty;
    m_fillBoundingBox = FloatRect();
    m_strokeBoundingBox = std::nullopt;
    m_approximateStrokeBoundingBox = std::nullopt;
    m_center = FloatPoint();
    m_radii = FloatSize();

    calculateRadiiAndCenter();

    // Spec: "A negative value is illegal. A value of zero disables rendering of the element."
    if (m_radii.isEmpty())
        return;

    if (m_radii.width() == m_radii.height())
        m_shapeType = ShapeType::Circle;
    else
        m_shapeType = ShapeType::Ellipse;

    if (hasNonScalingStroke()) {
        // Fallback to path-based approach if shape has a non-scaling stroke.
        m_fillBoundingBox = ensurePath().boundingRect();
        return;
    }

    m_fillBoundingBox = FloatRect(m_center.x() - m_radii.width(), m_center.y() - m_radii.height(), 2 * m_radii.width(), 2 * m_radii.height());
    m_strokeBoundingBox = m_fillBoundingBox;
    if (style().svgStyle().hasStroke())
        m_strokeBoundingBox->inflate(strokeWidth() / 2);
}

void LegacyRenderSVGEllipse::calculateRadiiAndCenter()
{
    Ref graphicsElement = this->graphicsElement();
    SVGLengthContext lengthContext(graphicsElement.ptr());
    m_center = FloatPoint(
        lengthContext.valueForLength(style().svgStyle().cx(), SVGLengthMode::Width),
        lengthContext.valueForLength(style().svgStyle().cy(), SVGLengthMode::Height));
    if (is<SVGCircleElement>(graphicsElement)) {
        float radius = lengthContext.valueForLength(style().svgStyle().r());
        m_radii = FloatSize(radius, radius);
        return;
    }

    ASSERT(is<SVGEllipseElement>(graphicsElement));

    auto& rx = style().svgStyle().rx();
    auto& ry = style().svgStyle().ry();
    m_radii = FloatSize(
        lengthContext.valueForLength(rx.isAuto() ? ry : rx, SVGLengthMode::Width),
        lengthContext.valueForLength(ry.isAuto() ? rx : ry, SVGLengthMode::Height));
    if (rx.isAuto())
        m_radii.setWidth(m_radii.height());
    else if (ry.isAuto())
        m_radii.setHeight(m_radii.width());
}

void LegacyRenderSVGEllipse::fillShape(GraphicsContext& context) const
{
    if (hasPath()) {
        LegacyRenderSVGShape::fillShape(context);
        return;
    }
    context.fillEllipse(m_fillBoundingBox);
}

void LegacyRenderSVGEllipse::strokeShape(GraphicsContext& context) const
{
    if (!style().hasVisibleStroke())
        return;
    if (hasPath()) {
        LegacyRenderSVGShape::strokeShape(context);
        return;
    }
    context.strokeEllipse(m_fillBoundingBox);
}

bool LegacyRenderSVGEllipse::canUseStrokeHitTestFastPath() const
{
    // Non-scaling-stroke needs special handling.
    if (hasNonScalingStroke())
        return false;

    // We can compute intersections with continuous strokes on circles
    // without using a Path.
    return m_shapeType == ShapeType::Circle && style().svgStyle().strokeDashArray().isNone();
}

bool LegacyRenderSVGEllipse::shapeDependentStrokeContains(const FloatPoint& point, PointCoordinateSpace pointCoordinateSpace)
{
    if (m_radii.isEmpty())
        return false;

    // The optimized code below does not support dash strokes and non-circle shape.
    // Thus we fallback to path-based approach in that case.
    if (!canUseStrokeHitTestFastPath()) {
        ensurePath();
        return LegacyRenderSVGShape::shapeDependentStrokeContains(point, pointCoordinateSpace);
    }

    float halfStrokeWidth = strokeWidth() / 2;
    FloatPoint centerOffset = FloatPoint(m_center.x() - point.x(), m_center.y() - point.y());
    return std::abs(centerOffset.length() - m_radii.width()) <= halfStrokeWidth;
}

bool LegacyRenderSVGEllipse::shapeDependentFillContains(const FloatPoint& point, const WindRule) const
{
    if (m_radii.isEmpty())
        return false;

    FloatPoint center = FloatPoint(m_center.x() - point.x(), m_center.y() - point.y());

    // This works by checking if the point satisfies the ellipse equation.
    // (x/rX)^2 + (y/rY)^2 <= 1
    float xrX = center.x() / m_radii.width();
    float yrY = center.y() / m_radii.height();
    return xrX * xrX + yrY * yrY <= 1.0;
}

bool LegacyRenderSVGEllipse::isRenderingDisabled() const
{
    // A radius of zero disables rendering of the element, and results in an empty bounding box.
    return m_fillBoundingBox.isEmpty();
}

}
