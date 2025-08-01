/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2005 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 * Copyright (C) 2021-2022 Apple Inc. All rights reserved.
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
#include "FEComposite.h"

#include "FECompositeNeonArithmeticApplier.h"
#include "FECompositeSoftwareApplier.h"
#include "FECompositeSoftwareArithmeticApplier.h"
#include "Filter.h"
#include <wtf/text/TextStream.h>

namespace WebCore {

Ref<FEComposite> FEComposite::create(const CompositeOperationType& type, float k1, float k2, float k3, float k4, DestinationColorSpace colorSpace)
{
    return adoptRef(*new FEComposite(type, k1, k2, k3, k4, colorSpace));
}

FEComposite::FEComposite(const CompositeOperationType& type, float k1, float k2, float k3, float k4, DestinationColorSpace colorSpace)
    : FilterEffect(FilterEffect::Type::FEComposite, colorSpace)
    , m_type(type)
    , m_k1(k1)
    , m_k2(k2)
    , m_k3(k3)
    , m_k4(k4)
{
}

bool FEComposite::operator==(const FEComposite& other) const
{
    return FilterEffect::operator==(other)
        && m_type == other.m_type
        && m_k1 == other.m_k1
        && m_k2 == other.m_k2
        && m_k3 == other.m_k3
        && m_k4 == other.m_k4;
}

bool FEComposite::setOperation(CompositeOperationType type)
{
    if (m_type == type)
        return false;
    m_type = type;
    return true;
}

bool FEComposite::setK1(float k1)
{
    if (m_k1 == k1)
        return false;
    m_k1 = k1;
    return true;
}

bool FEComposite::setK2(float k2)
{
    if (m_k2 == k2)
        return false;
    m_k2 = k2;
    return true;
}

bool FEComposite::setK3(float k3)
{
    if (m_k3 == k3)
        return false;
    m_k3 = k3;
    return true;
}

bool FEComposite::setK4(float k4)
{
    if (m_k4 == k4)
        return false;
    m_k4 = k4;
    return true;
}

FloatRect FEComposite::calculateImageRect(const Filter& filter, std::span<const FloatRect> inputImageRects, const FloatRect& primitiveSubregion) const
{
    switch (m_type) {
    case CompositeOperationType::FECOMPOSITE_OPERATOR_IN:
    case CompositeOperationType::FECOMPOSITE_OPERATOR_ATOP:
        // For In and Atop the first FilterImage just influences the result of the
        // second FilterImage. So just use the rect of the second FilterImage here.
        return filter.clipToMaxEffectRect(inputImageRects[1], primitiveSubregion);

    case CompositeOperationType::FECOMPOSITE_OPERATOR_ARITHMETIC:
        // Arithmetic may influnce the entire filter primitive region. So we can't
        // optimize the paint region here.
        return filter.maxEffectRect(primitiveSubregion);

    default:
        // Take the union of both input effects.
        return FilterEffect::calculateImageRect(filter, inputImageRects, primitiveSubregion);
    }
}

std::unique_ptr<FilterEffectApplier> FEComposite::createSoftwareApplier() const
{
    if (m_type != CompositeOperationType::FECOMPOSITE_OPERATOR_ARITHMETIC)
        return FilterEffectApplier::create<FECompositeSoftwareApplier>(*this);
#if HAVE(ARM_NEON_INTRINSICS)
    return FilterEffectApplier::create<FECompositeNeonArithmeticApplier>(*this);
#else
    return FilterEffectApplier::create<FECompositeSoftwareArithmeticApplier>(*this);
#endif
}

static TextStream& operator<<(TextStream& ts, const CompositeOperationType& type)
{
    switch (type) {
    case CompositeOperationType::FECOMPOSITE_OPERATOR_UNKNOWN:
        ts << "UNKNOWN"_s;
        break;
    case CompositeOperationType::FECOMPOSITE_OPERATOR_OVER:
        ts << "OVER"_s;
        break;
    case CompositeOperationType::FECOMPOSITE_OPERATOR_IN:
        ts << "IN"_s;
        break;
    case CompositeOperationType::FECOMPOSITE_OPERATOR_OUT:
        ts << "OUT"_s;
        break;
    case CompositeOperationType::FECOMPOSITE_OPERATOR_ATOP:
        ts << "ATOP"_s;
        break;
    case CompositeOperationType::FECOMPOSITE_OPERATOR_XOR:
        ts << "XOR"_s;
        break;
    case CompositeOperationType::FECOMPOSITE_OPERATOR_ARITHMETIC:
        ts << "ARITHMETIC"_s;
        break;
    case CompositeOperationType::FECOMPOSITE_OPERATOR_LIGHTER:
        ts << "LIGHTER"_s;
        break;
    }
    return ts;
}

TextStream& FEComposite::externalRepresentation(TextStream& ts, FilterRepresentation representation) const
{
    ts << indent << "[feComposite"_s;
    FilterEffect::externalRepresentation(ts, representation);

    ts << " operation=\""_s << m_type << '"';
    if (m_type == CompositeOperationType::FECOMPOSITE_OPERATOR_ARITHMETIC)
        ts << " k1=\""_s << m_k1 << "\" k2=\""_s << m_k2 << "\" k3=\""_s << m_k3 << "\" k4=\""_s << m_k4 << '"';

    ts << "]\n"_s;
    return ts;
}

} // namespace WebCore
