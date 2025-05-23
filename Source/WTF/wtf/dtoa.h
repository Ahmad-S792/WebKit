/*
 *  Copyright (C) 2003-2024 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include <array>
#include <wtf/ASCIICType.h>
#include <wtf/FastFloat.h>
#include <wtf/dtoa/double-conversion.h>
#include <wtf/text/StringView.h>

namespace WTF {

// Only toFixed() can use all the 124 positions. The format is:
// <-> + <21 digits> + decimal point + <100 digits> + null char = 124.
using NumberToStringBuffer = std::array<char, 124>;


// <-> + <320 digits> + decimal point + <6 digits> + null char = 329
using NumberToCSSStringBuffer = std::array<char, 329>;

using NumberToStringSpan = std::span<const char>;

WTF_EXPORT_PRIVATE NumberToStringSpan numberToFixedPrecisionString(float, unsigned significantFigures, NumberToStringBuffer& buffer LIFETIME_BOUND, bool truncateTrailingZeros = false);
WTF_EXPORT_PRIVATE NumberToStringSpan numberToFixedWidthString(float, unsigned decimalPlaces, NumberToStringBuffer& buffer LIFETIME_BOUND);

WTF_EXPORT_PRIVATE NumberToStringSpan numberToStringWithTrailingPoint(double, NumberToStringBuffer& buffer LIFETIME_BOUND);
WTF_EXPORT_PRIVATE NumberToStringSpan numberToFixedPrecisionString(double, unsigned significantFigures, NumberToStringBuffer& buffer LIFETIME_BOUND, bool truncateTrailingZeros = false);
WTF_EXPORT_PRIVATE NumberToStringSpan numberToFixedWidthString(double, unsigned decimalPlaces, NumberToStringBuffer& buffer LIFETIME_BOUND);

WTF_EXPORT_PRIVATE NumberToStringSpan numberToStringAndSize(float, NumberToStringBuffer& buffer LIFETIME_BOUND);
WTF_EXPORT_PRIVATE NumberToStringSpan numberToStringAndSize(double, NumberToStringBuffer& buffer LIFETIME_BOUND);

// Fixed width with up to 6 decimal places, trailing zeros truncated.
WTF_EXPORT_PRIVATE NumberToStringSpan numberToCSSString(double, NumberToCSSStringBuffer& buffer LIFETIME_BOUND);

inline double parseDouble(StringView string, size_t& parsedLength)
{
    if (string.is8Bit())
        return parseDouble(string.span8(), parsedLength);
    return parseDouble(string.span16(), parsedLength);
}

} // namespace WTF

using WTF::NumberToStringBuffer;
using WTF::numberToStringWithTrailingPoint;
using WTF::numberToStringAndSize;
using WTF::numberToFixedPrecisionString;
using WTF::numberToFixedWidthString;
using WTF::parseDouble;
