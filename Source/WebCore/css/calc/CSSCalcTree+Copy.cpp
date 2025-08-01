/*
 * Copyright (C) 2024-2025 Samuel Weinig <sam@webkit.org>
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CSSCalcTree+Copy.h"

#include "CSSCalcTree.h"

namespace WebCore {
namespace CSSCalc {

static auto copy(const Random::Sharing&) -> Random::Sharing;
static auto copy(const CSSValueID&) -> CSSValueID;
static auto copy(const CSS::Keyword::None&) -> CSS::Keyword::None;
static auto copy(const std::optional<Child>& root) -> std::optional<Child>;
static auto copy(const ChildOrNone&) -> ChildOrNone;
static auto copy(const Children&) -> Children;
static auto copy(const Child&) -> Child;
template<Leaf Op> Child copy(const Op&);
template<typename Op> static auto copy(const IndirectNode<Op>&) -> Child;
static auto copy(const IndirectNode<Anchor>&) -> Child;
static auto copy(const IndirectNode<AnchorSize>&) -> Child;

// MARK: Copying

Random::Sharing copy(const Random::Sharing& root)
{
    return root;
}

CSSValueID copy(const CSSValueID& root)
{
    return root;
}

CSS::Keyword::None copy(const CSS::Keyword::None& root)
{
    return root;
}

std::optional<Child> copy(const std::optional<Child>& root)
{
    if (root)
        return copy(*root);
    return std::nullopt;
}

ChildOrNone copy(const ChildOrNone& root)
{
    return WTF::switchOn(root, [&](const auto& root) { return ChildOrNone { copy(root) }; });
}

Children copy(const Children& children)
{
    return WTF::map(children, [&](const auto& child) { return copy(child); });
}

Child copy(const Child& root)
{
    return WTF::switchOn(root, [&](const auto& root) { return copy(root); });
}

template<Leaf Op> Child copy(const Op& root)
{
    return { root };
}

template<typename Op> Child copy(const IndirectNode<Op>& root)
{
    return makeChild(WTF::apply([](const auto& ...x) { return Op { copy(x)... }; } , *root), root.type);
}

AnchorSide copy(const AnchorSide& root)
{
    return WTF::switchOn(root, [&](const auto& root) { return AnchorSide { copy(root) }; });
}

Child copy(const IndirectNode<Anchor>& anchor)
{
    return makeChild(Anchor { .elementName = anchor->elementName, .side = copy(anchor->side), .fallback = copy(anchor->fallback) }, anchor.type);
}

Child copy(const IndirectNode<AnchorSize>& anchorSize)
{
    AnchorSize copyAnchorSize {
        .elementName = anchorSize->elementName,
        .dimension = anchorSize->dimension,
        .fallback = copy(anchorSize->fallback)
    };

    return makeChild(WTFMove(copyAnchorSize), anchorSize.type);
}

// MARK: Exposed functions

Tree copy(const Tree& tree)
{
    return Tree {
        .root = copy(tree.root),
        .type = tree.type,
        .stage = tree.stage,
        .requiresConversionData = tree.requiresConversionData
    };
}

} // namespace CSSCalc
} // namespace WebCore
