/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003-2018 Apple Inc. All rights reserved.
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
#include "Comment.h"

#include "Document.h"
#include "SerializedNode.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(Comment);

inline Comment::Comment(Document& document, String&& text)
    : CharacterData(document, WTFMove(text), COMMENT_NODE)
{
}

Ref<Comment> Comment::create(Document& document, String&& text)
{
    return adoptRef(*new Comment(document, WTFMove(text)));
}

String Comment::nodeName() const
{
    return "#comment"_s;
}

Ref<Node> Comment::cloneNodeInternal(Document& document, CloningOperation, CustomElementRegistry*) const
{
    return create(document, String { data() });
}

SerializedNode Comment::serializeNode(CloningOperation) const
{
    return { SerializedNode::Comment { data() } };
}

} // namespace WebCore
