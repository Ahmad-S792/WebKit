/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2005, 2006, 2007, 2008, 2022 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Graham Dennis (graham.dennis@gmail.com)
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
 *
 */

#include "config.h"
#include "BorderValue.h"

#include "StylePrimitiveNumericTypes+Logging.h"
#include <wtf/text/TextStream.h>

namespace WebCore {

bool BorderValue::isTransparent() const
{
    return m_color.isResolvedColor() && m_color.resolvedColor().isValid() && !m_color.resolvedColor().isVisible();
}

bool BorderValue::isVisible() const
{
    return nonZero() && !isTransparent() && style() != BorderStyle::Hidden;
}

TextStream& operator<<(TextStream& ts, const BorderValue& borderValue)
{
    return ts << borderValue.width() << ' ' << borderValue.style() << ' ' << borderValue.color();
}

} // namespace WebCore
