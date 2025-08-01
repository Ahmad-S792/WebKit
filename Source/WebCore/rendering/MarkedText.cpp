/*
 * Copyright (C) 2017-2021 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MarkedText.h"

#include "Document.h"
#include "DocumentInlines.h"
#include "DocumentMarkerController.h"
#include "Editor.h"
#include "ElementRuleCollector.h"
#include "HighlightRegistry.h"
#include "RenderBoxModelObject.h"
#include "RenderHighlight.h"
#include "RenderObjectInlines.h"
#include "RenderStyleInlines.h"
#include "RenderText.h"
#include "RenderedDocumentMarker.h"
#include "TextBoxSelectableRange.h"
#include <algorithm>
#include <ranges>
#include <wtf/HashSet.h>

namespace WebCore {

Vector<MarkedText> MarkedText::subdivide(const Vector<MarkedText>& markedTexts, OverlapStrategy overlapStrategy)
{
    if (markedTexts.isEmpty())
        return { };

    struct Offset {
        enum Kind { Begin, End };
        Kind kind;
        unsigned value; // Copy of markedText.startOffset/endOffset to avoid the need to branch based on kind.
        CheckedPtr<const MarkedText> markedText;
    };

    // 1. Build table of all offsets.
    Vector<Offset> offsets;
    ASSERT(markedTexts.size() < std::numeric_limits<unsigned>::max() / 2);
    unsigned numberOfMarkedTexts = markedTexts.size();
    unsigned numberOfOffsets = 2 * numberOfMarkedTexts;
    offsets.reserveInitialCapacity(numberOfOffsets);
    for (auto& markedText : markedTexts) {
        offsets.append({ Offset::Begin, markedText.startOffset, &markedText });
        offsets.append({ Offset::End, markedText.endOffset, &markedText });
    }

    // 2. Sort offsets such that begin offsets are in paint order and end offsets are in reverse paint order.
    std::ranges::sort(offsets, [] (const Offset& a, const Offset& b) {
        return a.value < b.value || (a.value == b.value && a.kind == b.kind && a.kind == Offset::Begin && a.markedText->type < b.markedText->type)
        || (a.value == b.value && a.kind == b.kind && a.kind == Offset::End && a.markedText->type > b.markedText->type);
    });

    // 3. Compute intersection.
    Vector<MarkedText> result;
    result.reserveInitialCapacity(numberOfMarkedTexts);
    HashSet<CheckedPtr<const MarkedText>> processedMarkedTexts;
    unsigned offsetSoFar = offsets[0].value;
    for (unsigned i = 1; i < numberOfOffsets; ++i) {
        if (offsets[i].value > offsets[i - 1].value) {
            if (overlapStrategy == OverlapStrategy::Frontmost) {
                std::optional<unsigned> frontmost;
                for (unsigned j = 0; j < i; ++j) {
                    if (!processedMarkedTexts.contains(offsets[j].markedText) && (!frontmost || offsets[j].markedText->type > offsets[*frontmost].markedText->type))
                        frontmost = j;
                }
                if (frontmost)
                    result.append({ offsetSoFar, offsets[i].value, offsets[*frontmost].markedText->type, offsets[*frontmost].markedText->marker, offsets[*frontmost].markedText->highlightName });
            } else {
                // The appended marked texts may not be in paint order. We will fix this up at the end of this function.
                for (unsigned j = 0; j < i; ++j) {
                    if (!processedMarkedTexts.contains(offsets[j].markedText))
                        result.append({ offsetSoFar, offsets[i].value, offsets[j].markedText->type, offsets[j].markedText->marker, offsets[j].markedText->highlightName, offsets[j].markedText->priority });
                }
            }
            offsetSoFar = offsets[i].value;
        }
        if (offsets[i].kind == Offset::End)
            processedMarkedTexts.add(offsets[i].markedText);
    }
    // Fix up; sort the marked texts so that they are in paint order.
    if (overlapStrategy == OverlapStrategy::None)
        std::ranges::sort(result, [] (const MarkedText& a, const MarkedText& b) { return a.startOffset < b.startOffset || (a.startOffset == b.startOffset && a.type < b.type); });
    return result;
}

Vector<MarkedText> MarkedText::collectForHighlights(const RenderText& renderer, const TextBoxSelectableRange& selectableRange, PaintPhase phase)
{
    Vector<MarkedText> markedTexts;
    RenderHighlight renderHighlight;
    auto& parentRenderer = *renderer.parent();
    auto& parentStyle = parentRenderer.style();
    if (auto highlightRegistry = renderer.document().highlightRegistryIfExists()) {
        for (auto& highlightName : highlightRegistry->highlightNames()) {
            auto renderStyle = parentRenderer.getUncachedPseudoStyle({ PseudoId::Highlight, highlightName }, &parentStyle);
            if (!renderStyle)
                continue;
            if (renderStyle->textDecorationLineInEffect().isEmpty() && phase == PaintPhase::Decoration)
                continue;
            for (auto& highlightRange : highlightRegistry->map().get(highlightName)->highlightRanges()) {
                if (!renderHighlight.setRenderRange(highlightRange))
                    continue;
                if (auto* staticRange = dynamicDowncast<StaticRange>(highlightRange->range()); staticRange
                    && (!staticRange->computeValidity() || staticRange->collapsed()))
                    continue;
                // FIXME: Potentially move this check elsewhere, to where we collect this range information.
                auto hasRenderer = [&] {
                    IntersectingNodeRange nodes(makeSimpleRange(highlightRange->range()));
                    for (auto& iterator : nodes) {
                        if (iterator.renderer())
                            return true;
                    }
                    return false;
                }();
                if (!hasRenderer)
                    continue;

                auto [highlightStart, highlightEnd] = renderHighlight.rangeForTextBox(renderer, selectableRange);

                if (highlightStart < highlightEnd) {
                    int currentPriority = highlightRegistry->map().get(highlightName)->priority();
                    // If we can just append it to the end, do that instead.
                    if (markedTexts.isEmpty() || markedTexts.last().priority <= currentPriority)
                        markedTexts.append({ highlightStart, highlightEnd, MarkedText::Type::Highlight, nullptr, highlightName, currentPriority });
                    else {
                        // Gets the first place such that it > currentPriority.
                        auto it = std::upper_bound(markedTexts.begin(), markedTexts.end(), currentPriority, [](const auto targetMarkedTextPriority, const auto& markedText) {
                            return targetMarkedTextPriority > markedText.priority;
                        });

                        unsigned insertIndex = (it == markedTexts.end() ? 0 : std::distance(markedTexts.begin(), it) - 1);

                        markedTexts.insert(insertIndex, { highlightStart, highlightEnd, MarkedText::Type::Highlight, nullptr, highlightName, currentPriority });
                    }
                }
            }
        }
    }
    
    if (renderer.document().settings().scrollToTextFragmentEnabled()) {
        if (auto fragmentHighlightRegistry = renderer.document().fragmentHighlightRegistryIfExists()) {
            for (auto& highlight : fragmentHighlightRegistry->map()) {
                for (auto& highlightRange : highlight.value->highlightRanges()) {
                    if (!renderHighlight.setRenderRange(highlightRange))
                        continue;

                    auto [highlightStart, highlightEnd] = renderHighlight.rangeForTextBox(renderer, selectableRange);
                    if (highlightStart < highlightEnd)
                        markedTexts.append({ highlightStart, highlightEnd, MarkedText::Type::FragmentHighlight });
                }
            }
        }
    }
    
#if ENABLE(APP_HIGHLIGHTS)
    if (auto appHighlightRegistry = renderer.document().appHighlightRegistryIfExists()) {
        if (appHighlightRegistry->highlightsVisibility() == HighlightVisibility::Visible) {
            for (auto& highlight : appHighlightRegistry->map()) {
                for (auto& highlightRange : highlight.value->highlightRanges()) {
                    if (!renderHighlight.setRenderRange(highlightRange))
                        continue;

                    auto [highlightStart, highlightEnd] = renderHighlight.rangeForTextBox(renderer, selectableRange);
                    if (highlightStart < highlightEnd)
                        markedTexts.append({ highlightStart, highlightEnd, MarkedText::Type::AppHighlight });
                }
            }
        }
    }
#endif
    return markedTexts;
}

Vector<MarkedText> MarkedText::collectForDocumentMarkers(const RenderText& renderer, const TextBoxSelectableRange& selectableRange, PaintPhase phase)
{
    if (!renderer.textNode())
        return { };

    CheckedPtr markerController = renderer.document().markersIfExists();
    if (!markerController)
        return { };

    auto markers = markerController->markersFor(*renderer.textNode());

    auto markedTextTypeForMarkerType = [] (DocumentMarkerType type) {
        switch (type) {
        case DocumentMarkerType::Spelling:
            return MarkedText::Type::SpellingError;
        case DocumentMarkerType::Grammar:
            return MarkedText::Type::GrammarError;
        case DocumentMarkerType::CorrectionIndicator:
            return MarkedText::Type::Correction;
#if ENABLE(WRITING_TOOLS)
        case DocumentMarkerType::WritingToolsTextSuggestion:
            return MarkedText::Type::WritingToolsTextSuggestion;
#endif
        case DocumentMarkerType::TextMatch:
            return MarkedText::Type::TextMatch;
        case DocumentMarkerType::DictationAlternatives:
            return MarkedText::Type::DictationAlternatives;
#if PLATFORM(IOS_FAMILY)
        case DocumentMarkerType::DictationPhraseWithAlternatives:
            return MarkedText::Type::DictationPhraseWithAlternatives;
#endif
        default:
            return MarkedText::Type::Unmarked;
        }
    };

    Vector<MarkedText> markedTexts;
    markedTexts.reserveInitialCapacity(markers.size());

    // Give any document markers that touch this run a chance to draw before the text has been drawn.
    // Note end() points at the last char, not one past it like endOffset and ranges do.
    for (auto& marker : markers) {
        // Collect either the background markers or the foreground markers, but not both
        switch (marker->type()) {
        case DocumentMarkerType::Grammar:
        case DocumentMarkerType::Spelling:
            break;
        case DocumentMarkerType::CorrectionIndicator:
#if ENABLE(WRITING_TOOLS)
        case DocumentMarkerType::WritingToolsTextSuggestion:
#endif
        case DocumentMarkerType::Replacement:
        case DocumentMarkerType::DictationAlternatives:
#if PLATFORM(IOS_FAMILY)
        // FIXME: Remove the PLATFORM(IOS_FAMILY)-guard.
        case DocumentMarkerType::DictationPhraseWithAlternatives:
#endif
            if (phase != MarkedText::PaintPhase::Decoration)
                continue;
            break;
        case DocumentMarkerType::TextMatch:
            if (!renderer.frame().editor().markedTextMatchesAreHighlighted())
                continue;
            if (phase == MarkedText::PaintPhase::Decoration)
                continue;
            break;
#if ENABLE(TELEPHONE_NUMBER_DETECTION)
        case DocumentMarkerType::TelephoneNumber:
            if (!renderer.frame().editor().markedTextMatchesAreHighlighted())
                continue;
            if (phase != MarkedText::PaintPhase::Background)
                continue;
            break;
#endif
        default:
            continue;
        }

        if (marker->endOffset() <= selectableRange.start) {
            // Marker is completely before this run. This might be a marker that sits before the
            // first run we draw, or markers that were within runs we skipped due to truncation.
            continue;
        }

        if (marker->startOffset() >= selectableRange.start + selectableRange.length) {
            // Marker is completely after this run, bail. A later run will paint it.
            break;
        }

        // Marker intersects this run. Collect it.
        switch (marker->type()) {
        case DocumentMarkerType::Spelling:
        case DocumentMarkerType::CorrectionIndicator:
#if ENABLE(WRITING_TOOLS)
        case DocumentMarkerType::WritingToolsTextSuggestion: {
            auto shouldPaintMarker = [&] {
                if (marker->type() != DocumentMarkerType::WritingToolsTextSuggestion)
                    return true;

                auto data = std::get<DocumentMarker::WritingToolsTextSuggestionData>(marker->data());

                if (data.state != DocumentMarker::WritingToolsTextSuggestionData::State::Accepted)
                    return false;

                if (data.decoration == DocumentMarker::WritingToolsTextSuggestionData::Decoration::None)
                    return false;

                return true;
            }();

            if (!shouldPaintMarker)
                break;

            [[fallthrough]];
        }
#endif
        case DocumentMarkerType::DictationAlternatives:
        case DocumentMarkerType::Grammar:
#if PLATFORM(IOS_FAMILY)
        // FIXME: See <rdar://problem/8933352>. Also, remove the PLATFORM(IOS_FAMILY)-guard.
        case DocumentMarkerType::DictationPhraseWithAlternatives:
#endif
        case DocumentMarkerType::TextMatch: {
            auto [clampedStart, clampedEnd] = selectableRange.clamp(marker->startOffset(), marker->endOffset());

            auto markedTextType = markedTextTypeForMarkerType(marker->type());
            markedTexts.append({ clampedStart, clampedEnd, markedTextType, marker.get() });
            break;
        }
        case DocumentMarkerType::Replacement:
            break;
#if ENABLE(TELEPHONE_NUMBER_DETECTION)
        case DocumentMarkerType::TelephoneNumber:
            break;
#endif
        default:
            ASSERT_NOT_REACHED();
        }
    }
    return markedTexts;
}

Vector<MarkedText> MarkedText::collectForDraggedAndTransparentContent(const DocumentMarkerType type, const RenderText& renderer, const TextBoxSelectableRange& selectableRange)
{
    auto markerTypeForDocumentMarker = [] (DocumentMarkerType type) {
        switch (type) {
        case DocumentMarkerType::DraggedContent:
            return MarkedText::Type::DraggedContent;
        case DocumentMarkerType::TransparentContent:
            return MarkedText::Type::TransparentContent;
        default:
            return MarkedText::Type::Unmarked;
        }
    };
    Type markerType = markerTypeForDocumentMarker(type);
    if (markerType == MarkedText::Type::Unmarked) {
        ASSERT_NOT_REACHED();
        return { };
    }
    auto contentRanges = renderer.contentRangesBetweenOffsetsForType(type, selectableRange.start, selectableRange.start + selectableRange.length);

    return contentRanges.map([&](const auto& range) -> MarkedText {
        return { selectableRange.clamp(range.first), selectableRange.clamp(range.second), markerType };
    });
}

}
