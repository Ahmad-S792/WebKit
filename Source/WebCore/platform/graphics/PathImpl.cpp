/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PathImpl.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(PathImpl);

void PathImpl::addLinesForRect(const FloatRect& rect)
{
    add(PathMoveTo { rect.minXMinYCorner() });
    add(PathLineTo { rect.maxXMinYCorner() });
    add(PathLineTo { rect.maxXMaxYCorner() });
    add(PathLineTo { rect.minXMaxYCorner() });
    add(PathCloseSubpath { });
}

Vector<PathSegment, 10> PathImpl::beziersForRoundedRect(const FloatRoundedRect& roundedRect)
{
    const auto& radii = roundedRect.radii();
    const auto& rect = roundedRect.rect();

    const auto& topLeftRadius = radii.topLeft();
    const auto& topRightRadius = radii.topRight();
    const auto& bottomLeftRadius = radii.bottomLeft();
    const auto& bottomRightRadius = radii.bottomRight();

    Vector<PathSegment, 10> segments;
    segments.append(PathSegment(PathMoveTo { FloatPoint(rect.x() + topLeftRadius.width(), rect.y()) }));

    segments.append(PathSegment(PathLineTo { FloatPoint(rect.maxX() - topRightRadius.width(), rect.y()) }));
    if (topRightRadius.width() > 0 || topRightRadius.height() > 0) {
        segments.append(PathSegment(PathBezierCurveTo { FloatPoint(rect.maxX() - topRightRadius.width() * circleControlPoint(), rect.y()),
            FloatPoint(rect.maxX(), rect.y() + topRightRadius.height() * circleControlPoint()),
            FloatPoint(rect.maxX(), rect.y() + topRightRadius.height()) }));
    }

    segments.append(PathSegment(PathLineTo { FloatPoint(rect.maxX(), rect.maxY() - bottomRightRadius.height()) }));

    if (bottomRightRadius.width() > 0 || bottomRightRadius.height() > 0) {
        segments.append(PathSegment(PathBezierCurveTo { FloatPoint(rect.maxX(), rect.maxY() - bottomRightRadius.height() * circleControlPoint()),
            FloatPoint(rect.maxX() - bottomRightRadius.width() * circleControlPoint(), rect.maxY()),
            FloatPoint(rect.maxX() - bottomRightRadius.width(), rect.maxY()) }));
    }

    segments.append(PathSegment(PathLineTo { FloatPoint(rect.x() + bottomLeftRadius.width(), rect.maxY()) }));
    if (bottomLeftRadius.width() > 0 || bottomLeftRadius.height() > 0) {
        segments.append(PathSegment(PathBezierCurveTo { FloatPoint(rect.x() + bottomLeftRadius.width() * circleControlPoint(), rect.maxY()),
            FloatPoint(rect.x(), rect.maxY() - bottomLeftRadius.height() * circleControlPoint()),
            FloatPoint(rect.x(), rect.maxY() - bottomLeftRadius.height()) }));
    }

    segments.append(PathSegment(PathLineTo { FloatPoint(rect.x(), rect.y() + topLeftRadius.height()) }));

    if (topLeftRadius.width() > 0 || topLeftRadius.height() > 0) {
        segments.append(PathSegment(PathBezierCurveTo { FloatPoint(rect.x(), rect.y() + topLeftRadius.height() * circleControlPoint()),
            FloatPoint(rect.x() + topLeftRadius.width() * circleControlPoint(), rect.y()),
            FloatPoint(rect.x() + topLeftRadius.width(), rect.y()) }));
    }

    segments.append(PathSegment(PathCloseSubpath { }));
    ASSERT(segments.size() <= 10); // Update the preallocated vector size and call sites if the amount changes.
    return segments;
}

void PathImpl::applySegments(const PathSegmentApplier& applier) const
{
    applyElements([&](const PathElement& pathElement) {
        switch (pathElement.type) {
        case PathElement::Type::MoveToPoint:
            applier({ PathMoveTo { pathElement.points[0] } });
            break;

        case PathElement::Type::AddLineToPoint:
            applier({ PathLineTo { pathElement.points[0] } });
            break;

        case PathElement::Type::AddQuadCurveToPoint:
            applier({ PathQuadCurveTo { pathElement.points[0], pathElement.points[1] } });
            break;

        case PathElement::Type::AddCurveToPoint:
            applier({ PathBezierCurveTo { pathElement.points[0], pathElement.points[1], pathElement.points[2] } });
            break;

        case PathElement::Type::CloseSubpath:
            applier({ PathCloseSubpath { } });
            break;
        }
    });
}

bool PathImpl::isClosed() const
{
    bool lastElementIsClosed = false;

    // The path is closed if the type of the last PathElement is CloseSubpath. Unfortunately,
    // the only way to access PathElements is sequentially through apply(), there's no random
    // access as if they're in a vector.
    // The lambda below sets lastElementIsClosed if the last PathElement is CloseSubpath.
    // Because lastElementIsClosed is overridden if there are any remaining PathElements
    // to be iterated, its final value is the value of the last iteration.
    // (i.e the last PathElement).
    // FIXME: find a more efficient way to implement this, that does not require iterating
    // through all PathElements.
    applyElements([&lastElementIsClosed](const PathElement& element) {
        lastElementIsClosed = (element.type == PathElement::Type::CloseSubpath);
    });

    return lastElementIsClosed;
}

bool PathImpl::hasSubpaths() const
{
    auto rect = fastBoundingRect();
    return rect.height() || rect.width();
}

} // namespace WebCore
