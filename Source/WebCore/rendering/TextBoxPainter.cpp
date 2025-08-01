/*
 * Copyright (C) 2021-2023 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "TextBoxPainter.h"

#include "CaretRectComputation.h"
#include "CompositionHighlight.h"
#include "DocumentInlines.h"
#include "DocumentMarkerController.h"
#include "Editor.h"
#include "EventRegion.h"
#include "GraphicsContext.h"
#include "HTMLAnchorElement.h"
#include "InlineIteratorBoxInlines.h"
#include "InlineIteratorLineBox.h"
#include "InlineIteratorTextBoxInlines.h"
#include "InlineTextBoxStyle.h"
#include "LineSelection.h"
#include "PaintInfo.h"
#include "RenderBlock.h"
#include "RenderBoxModelObjectInlines.h"
#include "RenderCombineText.h"
#include "RenderElementInlines.h"
#include "RenderText.h"
#include "RenderTheme.h"
#include "RenderView.h"
#include "RenderedDocumentMarker.h"
#include "StyleTextDecorationThickness.h"
#include "StyledMarkedText.h"
#include "TextPaintStyle.h"
#include "TextPainter.h"

#if ENABLE(WRITING_TOOLS)
#include "GraphicsContextCG.h"
#endif

namespace WebCore {

static FloatRect calculateDocumentMarkerBounds(const InlineIterator::TextBoxIterator&, const MarkedText&);

TextBoxPainter::TextBoxPainter(const LayoutIntegration::InlineContent& inlineContent, const InlineDisplay::Box& box, const RenderStyle& style, PaintInfo& paintInfo, const LayoutPoint& paintOffset)
    : m_textBox(InlineIterator::BoxModernPath { inlineContent, inlineContent.indexForBox(box) })
    , m_renderer(downcast<RenderText>(m_textBox.renderer()))
    , m_document(m_renderer.document())
    , m_style(style)
    , m_logicalRect(m_textBox.isHorizontal() ? m_textBox.visualRectIgnoringBlockDirection() : m_textBox.visualRectIgnoringBlockDirection().transposedRect())
    , m_paintTextRun(m_textBox.textRun())
    , m_paintInfo(paintInfo)
    , m_selectableRange(m_textBox.selectableRange())
    , m_paintOffset(paintOffset)
    , m_paintRect(computePaintRect(paintOffset))
    , m_isFirstLine(m_textBox.isFirstLine())
    , m_isCombinedText([&] {
        auto* combineTextRenderer = dynamicDowncast<RenderCombineText>(m_renderer);
        return combineTextRenderer && combineTextRenderer->isCombined();
    }())
    , m_isPrinting(m_document.printing())
    , m_haveSelection(computeHaveSelection())
    , m_emphasisMarkExistsAndIsAbove(RenderText::emphasisMarkExistsAndIsAbove(m_renderer, m_style))
{
    ASSERT(paintInfo.phase == PaintPhase::Foreground || paintInfo.phase == PaintPhase::Selection || paintInfo.phase == PaintPhase::TextClip || paintInfo.phase == PaintPhase::EventRegion || paintInfo.phase == PaintPhase::Accessibility);

    auto& editor = m_renderer.frame().editor();
    m_containsComposition = m_renderer.textNode() && editor.compositionNode() == m_renderer.textNode();
    m_useCustomUnderlines = m_containsComposition && editor.compositionUsesCustomUnderlines();
}

TextBoxPainter::~TextBoxPainter() = default;

InlineIterator::TextBoxIterator TextBoxPainter::makeIterator() const
{
    auto pathCopy = m_textBox;
    return InlineIterator::TextBoxIterator { WTFMove(pathCopy) };
}

void TextBoxPainter::paint()
{
    if (m_paintInfo.paintBehavior.contains(PaintBehavior::ExcludeText))
        return;

    if (m_paintInfo.phase == PaintPhase::Selection && !m_haveSelection)
        return;

    if (m_paintInfo.phase == PaintPhase::EventRegion) {
        constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::IgnoreCSSPointerEventsProperty };
        if (m_renderer.parent()->visibleToHitTesting(hitType))
            m_paintInfo.eventRegionContext()->unite(FloatRoundedRect(m_paintRect), const_cast<RenderText&>(m_renderer), m_style);
        return;
    }

    std::optional<RotationDirection> glyphRotation;
    if (!textBox().isHorizontal() && !m_isCombinedText) {
        glyphRotation = textBox().writingMode().isLineOverLeft()
            ? RotationDirection::Counterclockwise
            : RotationDirection::Clockwise;
        m_paintInfo.context().concatCTM(rotation(m_paintRect, *glyphRotation));
    }

    if (m_paintInfo.phase == PaintPhase::Accessibility) {
        if (glyphRotation) {
            auto transform = rotation(m_paintRect, *glyphRotation);
            m_paintInfo.accessibilityRegionContext()->takeBounds(m_renderer, transform.mapRect(m_paintRect), textBox().lineIndex());
        } else
            m_paintInfo.accessibilityRegionContext()->takeBounds(m_renderer, m_paintRect, textBox().lineIndex());

        return;
    }

    if (m_paintInfo.phase == PaintPhase::Foreground) {
        if (!m_isPrinting)
            paintBackground();

        paintPlatformDocumentMarkers();
    }

    paintForegroundAndDecorations();

    if (m_paintInfo.phase == PaintPhase::Foreground) {
        if (m_useCustomUnderlines)
            paintCompositionUnderlines();

        m_renderer.page().addRelevantRepaintedObject(m_renderer, enclosingLayoutRect(m_paintRect));
    }

    if (glyphRotation) {
        auto backRotation = *glyphRotation == RotationDirection::Clockwise
            ? RotationDirection::Counterclockwise
            : RotationDirection::Clockwise;
        m_paintInfo.context().concatCTM(rotation(m_paintRect, backRotation));
    }
}

std::pair<unsigned, unsigned> TextBoxPainter::selectionStartEnd() const
{
    return m_renderer.view().selection().rangeForTextBox(m_renderer, m_selectableRange);
}

MarkedText TextBoxPainter::createMarkedTextFromSelectionInBox()
{
    auto [selectionStart, selectionEnd] = selectionStartEnd();
    if (selectionStart < selectionEnd)
        return { selectionStart, selectionEnd, MarkedText::Type::Selection };
    return { };
}

void TextBoxPainter::paintBackground()
{
    auto shouldPaintCompositionBackground = m_containsComposition && !m_useCustomUnderlines;
#if ENABLE(TEXT_SELECTION)
    auto hasSelectionWithNonCustomUnderline = m_haveSelection && !m_useCustomUnderlines;
#endif

    auto shouldPaintBackground = [&] {
#if ENABLE(TEXT_SELECTION)
        if (hasSelectionWithNonCustomUnderline)
            return true;
#endif
        if (shouldPaintCompositionBackground)
            return true;
        if (CheckedPtr markers = m_document.markersIfExists(); markers && markers->hasMarkers())
            return true;
        if (m_document.hasHighlight())
            return true;
        return false;
    };
    if (!shouldPaintBackground())
        return;

    if (shouldPaintCompositionBackground)
        paintCompositionBackground();

    Vector<MarkedText> markedTexts;
    markedTexts.appendVector(MarkedText::collectForDocumentMarkers(m_renderer, m_selectableRange, MarkedText::PaintPhase::Background));
    markedTexts.appendVector(MarkedText::collectForHighlights(m_renderer, m_selectableRange, MarkedText::PaintPhase::Background));

#if ENABLE(TEXT_SELECTION)
    if (hasSelectionWithNonCustomUnderline && !m_paintInfo.context().paintingDisabled()) {
        auto selectionMarkedText = createMarkedTextFromSelectionInBox();
        if (!selectionMarkedText.isEmpty())
            markedTexts.append(WTFMove(selectionMarkedText));
    }
#endif
    auto styledMarkedTexts = StyledMarkedText::subdivideAndResolve(markedTexts, m_renderer, m_isFirstLine, m_paintInfo);

    // Coalesce styles of adjacent marked texts to minimize the number of drawing commands.
    auto coalescedStyledMarkedTexts = StyledMarkedText::coalesceAdjacentWithEqualBackground(styledMarkedTexts);

    for (auto& markedText : coalescedStyledMarkedTexts)
        paintBackground(markedText);
}

void TextBoxPainter::paintCompositionForeground(const StyledMarkedText& markedText)
{
    auto hasCompositionCustomHighlights = [&]() {
        if (!m_containsComposition)
            return false;

        auto& editor = m_renderer.frame().editor();
        return editor.compositionUsesCustomHighlights();
    };

    if (!hasCompositionCustomHighlights()) {
        paintForeground(markedText);
        return;
    }

    // The highlight ranges must be "packed" so that there is no non-empty interval between
    // any two adjacent highlight ranges. This is needed since otherwise, `paintForeground`
    // will not be called in those would-be non-empty intervals.
    auto& editor = m_renderer.frame().editor();
    auto highlights = editor.customCompositionHighlights();

    Vector<CompositionHighlight> highlightsWithForeground;
    highlightsWithForeground.append({ textBox().start(), highlights[0].startOffset, { }, { } });

    for (size_t i = 0; i < highlights.size(); ++i) {
        highlightsWithForeground.append(highlights[i]);
        if (i != highlights.size() - 1)
            highlightsWithForeground.append({ highlights[i].endOffset, highlights[i + 1].startOffset, { }, { } });
    }

    highlightsWithForeground.append({ highlights.last().endOffset, textBox().end(), { }, { } });

    for (auto& highlight : highlightsWithForeground) {
        auto style = StyledMarkedText::computeStyleForUnmarkedMarkedText(m_renderer, m_style, m_isFirstLine, m_paintInfo);

        if (highlight.endOffset <= textBox().start())
            continue;

        if (highlight.startOffset >= textBox().end())
            break;

        auto [clampedStart, clampedEnd] = m_selectableRange.clamp(highlight.startOffset, highlight.endOffset);

        if (highlight.foregroundColor)
            style.textStyles.fillColor = *highlight.foregroundColor;

        paintForeground({ MarkedText { clampedStart, clampedEnd, MarkedText::Type::Unmarked }, style });

        if (highlight.endOffset > textBox().end())
            break;
    }
}

void TextBoxPainter::paintForegroundAndDecorations()
{
    auto shouldPaintSelectionForeground = m_haveSelection && !m_useCustomUnderlines;
    auto hasTextDecoration = !m_style.textDecorationLineInEffect().isEmpty();
    auto hasHighlightDecoration = m_document.hasHighlight() && !MarkedText::collectForHighlights(m_renderer, m_selectableRange, MarkedText::PaintPhase::Decoration).isEmpty();
    auto hasMismatchingContentDirection = m_renderer.containingBlock()->writingMode().bidiDirection() != textBox().direction();
    auto hasBackwardTrunctation = m_selectableRange.truncation && hasMismatchingContentDirection;

    auto hasSpellingOrGrammarDecoration = [&] {
        auto markedTexts = MarkedText::collectForDocumentMarkers(m_renderer, m_selectableRange, MarkedText::PaintPhase::Decoration);

        auto hasSpellingError = markedTexts.containsIf([](auto&& markedText) {
            return markedText.type == MarkedText::Type::SpellingError;
        });

        if (hasSpellingError) {
            auto spellingErrorStyle = m_renderer.spellingErrorPseudoStyle();
            if (spellingErrorStyle)
                return !spellingErrorStyle->textDecorationLineInEffect().isEmpty();
        }

        auto hasGrammarError = markedTexts.containsIf([](auto&& markedText) {
            return markedText.type == MarkedText::Type::GrammarError;
        });

        if (hasGrammarError) {
            auto grammarErrorStyle = m_renderer.grammarErrorPseudoStyle();
            if (grammarErrorStyle)
                return !grammarErrorStyle->textDecorationLineInEffect().isEmpty();
        }

        return false;
    };

    auto hasDecoration = hasTextDecoration || hasHighlightDecoration || hasSpellingOrGrammarDecoration();

    auto contentMayNeedStyledMarkedText = [&] {
        if (hasDecoration)
            return true;
        if (shouldPaintSelectionForeground)
            return true;
        if (CheckedPtr markers = m_document.markersIfExists(); markers && markers->hasMarkers())
            return true;
        if (m_document.hasHighlight())
            return true;
        return false;
    };
    auto startPosition = [&] {
        return !hasBackwardTrunctation ? m_selectableRange.clamp(textBox().start()) : textBox().length() - *m_selectableRange.truncation;
    };
    auto endPosition = [&] {
        return !hasBackwardTrunctation ? m_selectableRange.clamp(textBox().end()) : textBox().length();
    };
    if (!contentMayNeedStyledMarkedText()) {
        auto markedText = MarkedText { startPosition(), endPosition(), MarkedText::Type::Unmarked };
        auto styledMarkedText = StyledMarkedText { markedText, StyledMarkedText::computeStyleForUnmarkedMarkedText(m_renderer, m_style, m_isFirstLine, m_paintInfo) };
        paintCompositionForeground(styledMarkedText);
        return;
    }

    Vector<MarkedText> markedTexts;
    if (m_paintInfo.phase != PaintPhase::Selection) {
        // The marked texts for the gaps between document markers and selection are implicitly created by subdividing the entire line.
        markedTexts.append({ startPosition(), endPosition(), MarkedText::Type::Unmarked });

        if (!m_isPrinting) {
            markedTexts.appendVector(MarkedText::collectForDocumentMarkers(m_renderer, m_selectableRange, MarkedText::PaintPhase::Foreground));
            markedTexts.appendVector(MarkedText::collectForHighlights(m_renderer, m_selectableRange, MarkedText::PaintPhase::Foreground));

            bool shouldPaintDraggedContent = !(m_paintInfo.paintBehavior.contains(PaintBehavior::ExcludeSelection));
            if (shouldPaintDraggedContent) {
                auto markedTextsForDraggedContent = MarkedText::collectForDraggedAndTransparentContent(DocumentMarkerType::DraggedContent, m_renderer, m_selectableRange);
                if (!markedTextsForDraggedContent.isEmpty()) {
                    shouldPaintSelectionForeground = false;
                    markedTexts.appendVector(WTFMove(markedTextsForDraggedContent));
                }
            }
            auto markedTextsForTransparentContent = MarkedText::collectForDraggedAndTransparentContent(DocumentMarkerType::TransparentContent, m_renderer, m_selectableRange);
            if (!markedTextsForTransparentContent.isEmpty())
                markedTexts.appendVector(WTFMove(markedTextsForTransparentContent));
        }
    }
    // The selection marked text acts as a placeholder when computing the marked texts for the gaps...
    if (shouldPaintSelectionForeground) {
        ASSERT(!m_isPrinting);
        auto selectionMarkedText = createMarkedTextFromSelectionInBox();
        if (!selectionMarkedText.isEmpty())
            markedTexts.append(WTFMove(selectionMarkedText));
    }

    auto styledMarkedTexts = StyledMarkedText::subdivideAndResolve(markedTexts, m_renderer, m_isFirstLine, m_paintInfo);

    // ... now remove the selection marked text if we are excluding selection.
    if (!m_isPrinting && m_paintInfo.paintBehavior.contains(PaintBehavior::ExcludeSelection)) {
        styledMarkedTexts.removeAllMatching([] (const StyledMarkedText& markedText) {
            return markedText.type == MarkedText::Type::Selection;
        });
    }

    if (hasDecoration && m_paintInfo.phase != PaintPhase::Selection) {
        unsigned length = m_selectableRange.truncation.value_or(m_paintTextRun.length());
        unsigned selectionStart = 0;
        unsigned selectionEnd = 0;
        if (m_haveSelection)
            std::tie(selectionStart, selectionEnd) = selectionStartEnd();

        FloatRect textDecorationSelectionClipOutRect;
        if ((m_paintInfo.paintBehavior.contains(PaintBehavior::ExcludeSelection)) && selectionStart < selectionEnd && selectionEnd <= length) {
            textDecorationSelectionClipOutRect = m_paintRect;
            float logicalWidthBeforeRange;
            float logicalWidthAfterRange;
            float logicalSelectionWidth = fontCascade().widthOfTextRange(m_paintTextRun, selectionStart, selectionEnd, nullptr, &logicalWidthBeforeRange, &logicalWidthAfterRange);
            // FIXME: Do we need to handle vertical bottom to top text?
            if (!textBox().isHorizontal()) {
                textDecorationSelectionClipOutRect.move(0, logicalWidthBeforeRange);
                textDecorationSelectionClipOutRect.setHeight(logicalSelectionWidth);
            } else if (textBox().direction() == TextDirection::RTL) {
                textDecorationSelectionClipOutRect.move(logicalWidthAfterRange, 0);
                textDecorationSelectionClipOutRect.setWidth(logicalSelectionWidth);
            } else {
                textDecorationSelectionClipOutRect.move(logicalWidthBeforeRange, 0);
                textDecorationSelectionClipOutRect.setWidth(logicalSelectionWidth);
            }
        }

        // Coalesce styles of adjacent marked texts to minimize the number of drawing commands.
        auto coalescedStyledMarkedTexts = StyledMarkedText::coalesceAdjacentWithEqualDecorations(styledMarkedTexts);

        for (auto& markedText : coalescedStyledMarkedTexts) {
            unsigned startOffset = markedText.startOffset;
            unsigned endOffset = markedText.endOffset;
            if (startOffset < endOffset) {
                // Avoid measuring the text when the entire line box is selected as an optimization.
                auto snappedPaintRect = snapRectToDevicePixelsWithWritingDirection(LayoutRect { m_paintRect }, m_document.deviceScaleFactor(), m_paintTextRun.ltr());
                if (startOffset || endOffset != m_paintTextRun.length()) {
                    LayoutRect selectionRect = { m_paintRect.x(), m_paintRect.y(), m_paintRect.width(), m_paintRect.height() };
                    fontCascade().adjustSelectionRectForText(m_renderer.canUseSimplifiedTextMeasuring().value_or(false), m_paintTextRun, selectionRect, startOffset, endOffset);
                    snappedPaintRect = snapRectToDevicePixelsWithWritingDirection(selectionRect, m_document.deviceScaleFactor(), m_paintTextRun.ltr());
                }
                auto decorationPainter = createDecorationPainter(markedText, textDecorationSelectionClipOutRect);
                paintBackgroundDecorations(decorationPainter, markedText, snappedPaintRect);
                paintCompositionForeground(markedText);
                paintForegroundDecorations(decorationPainter, markedText, snappedPaintRect);
            }
        }
    } else {
        // Coalesce styles of adjacent marked texts to minimize the number of drawing commands.
        auto coalescedStyledMarkedTexts = StyledMarkedText::coalesceAdjacentWithEqualForeground(styledMarkedTexts);

        if (coalescedStyledMarkedTexts.isEmpty())
            return;

        for (auto& markedText : coalescedStyledMarkedTexts)
            paintCompositionForeground(markedText);
    }
}

void TextBoxPainter::paintCompositionBackground()
{
    auto& editor = m_renderer.frame().editor();

    if (!editor.compositionUsesCustomHighlights()) {
        auto [clampedStart, clampedEnd] = m_selectableRange.clamp(editor.compositionStart(), editor.compositionEnd());

        paintBackground(clampedStart, clampedEnd, CompositionHighlight::defaultCompositionFillColor);
        return;
    }

    for (auto& highlight : editor.customCompositionHighlights()) {
        if (!highlight.backgroundColor)
            continue;

        if (highlight.endOffset <= textBox().start())
            continue;

        if (highlight.startOffset >= textBox().end())
            break;

        auto [clampedStart, clampedEnd] = m_selectableRange.clamp(highlight.startOffset, highlight.endOffset);

        paintBackground(clampedStart, clampedEnd, *highlight.backgroundColor, BackgroundStyle::Rounded);

        if (highlight.endOffset > textBox().end())
            break;
    }
}

void TextBoxPainter::paintBackground(const StyledMarkedText& markedText)
{
    paintBackground(markedText.startOffset, markedText.endOffset, markedText.style.backgroundColor, BackgroundStyle::Normal);
}

void TextBoxPainter::paintBackground(unsigned startOffset, unsigned endOffset, const Color& color, BackgroundStyle backgroundStyle)
{
    if (startOffset >= endOffset)
        return;

    GraphicsContext& context = m_paintInfo.context();
    GraphicsContextStateSaver stateSaver { context };
    updateGraphicsContext(context, TextPaintStyle { color }); // Don't draw text at all!

    // Note that if the text is truncated, we let the thing being painted in the truncation
    // draw its own highlight.
    auto lineBox = makeIterator()->lineBox();
    auto selectionBottom = LineSelection::logicalBottom(*lineBox);
    auto selectionTop = LineSelection::logicalTopAdjustedForPrecedingBlock(*lineBox);
    // Use same y positioning and height as for selection, so that when the selection and this subrange are on
    // the same word there are no pieces sticking out.
    auto deltaY = LayoutUnit { writingMode().isLineInverted() ? selectionBottom - m_logicalRect.maxY() : m_logicalRect.y() - selectionTop };
    auto selectionHeight = LayoutUnit { std::max(0.f, selectionBottom - selectionTop) };
    auto selectionRect = LayoutRect { LayoutUnit(m_paintRect.x()), LayoutUnit(m_paintRect.y() - deltaY), LayoutUnit(m_logicalRect.width()), selectionHeight };
    auto adjustedSelectionRect = selectionRect;
    fontCascade().adjustSelectionRectForText(m_renderer.canUseSimplifiedTextMeasuring().value_or(false), m_paintTextRun, adjustedSelectionRect, startOffset, endOffset);
    if (m_paintTextRun.length() == endOffset - startOffset) {
        // FIXME: We should reconsider re-measuring the content when non-whitespace runs are joined together (see webkit.org/b/251318).
        auto visualRight = std::max(adjustedSelectionRect.maxX(), selectionRect.maxX());
        adjustedSelectionRect.shiftMaxXEdgeTo(visualRight);
    }

    // FIXME: Support painting combined text. See <https://bugs.webkit.org/show_bug.cgi?id=180993>.
    auto backgroundRect = snapRectToDevicePixels(adjustedSelectionRect, m_document.deviceScaleFactor());
    if (backgroundStyle == BackgroundStyle::Rounded) {
        backgroundRect.expand(-1, -1);
        backgroundRect.move(0.5, 0.5);
        context.fillRoundedRect(FloatRoundedRect { backgroundRect, FloatRoundedRect::Radii { 2 } }, color);
        return;
    }

    context.fillRect(backgroundRect, color);
}

void TextBoxPainter::paintForeground(const StyledMarkedText& markedText)
{
    if (markedText.startOffset >= markedText.endOffset)
        return;

    auto& context = m_paintInfo.context();
    const FontCascade& font = fontCascade();

    float emphasisMarkOffset = 0;
    auto& emphasisMark = m_emphasisMarkExistsAndIsAbove ? m_style.textEmphasisStyle().markString() : nullAtom();
    if (!emphasisMark.isEmpty())
        emphasisMarkOffset = *m_emphasisMarkExistsAndIsAbove ? -font.metricsOfPrimaryFont().intAscent() - font.emphasisMarkDescent(emphasisMark) : font.metricsOfPrimaryFont().intDescent() + font.emphasisMarkAscent(emphasisMark);

    TextPainter textPainter {
        context,
        font,
        m_style,
        markedText.style.textStyles,
        markedText.style.textShadow,
        !markedText.style.textShadow.isNone() && m_style.hasAppleColorFilter() ? &m_style.appleColorFilter() : nullptr,
        emphasisMark,
        emphasisMarkOffset,
        m_isCombinedText ? &downcast<RenderCombineText>(m_renderer) : nullptr
    };

    bool isTransparentMarkedText = markedText.type == MarkedText::Type::DraggedContent || markedText.type == MarkedText::Type::TransparentContent;
    GraphicsContextStateSaver stateSaver(context, markedText.style.textStyles.strokeWidth > 0 || isTransparentMarkedText);
    if (isTransparentMarkedText)
        context.setAlpha(markedText.style.alpha);
    updateGraphicsContext(context, markedText.style.textStyles);

    textPainter.setGlyphDisplayListIfNeeded(textBox().box(), m_paintInfo, m_style, m_paintTextRun);

    // TextPainter wants the box rectangle and text origin of the entire line box.
    textPainter.paintRange(m_paintTextRun, m_paintRect, textOriginFromPaintRect(m_paintRect), markedText.startOffset, markedText.endOffset);
}

TextDecorationPainter TextBoxPainter::createDecorationPainter(const StyledMarkedText& markedText, const FloatRect& clipOutRect)
{
    auto& context = m_paintInfo.context();

    updateGraphicsContext(context, markedText.style.textStyles);

    // Note that if the text is truncated, we let the thing being painted in the truncation
    // draw its own decoration.
    GraphicsContextStateSaver stateSaver { context, false };
    bool isTransparentContent = markedText.type == MarkedText::Type::DraggedContent || markedText.type == MarkedText::Type::TransparentContent;
    if (isTransparentContent || !clipOutRect.isEmpty()) {
        stateSaver.save();
        if (isTransparentContent)
            context.setAlpha(markedText.style.alpha);
        if (!clipOutRect.isEmpty())
            context.clipOut(clipOutRect);
    }

    // Create painter
    return {
        context,
        fontCascade(),
        markedText.style.textShadow,
        !markedText.style.textShadow.isNone() && m_style.hasAppleColorFilter() ? &m_style.appleColorFilter() : nullptr,
        m_document.printing(),
        writingMode()
    };
}

static inline float computedTextDecorationThickness(const RenderStyle& styleToUse, float deviceScaleFactor)
{
    return ceilToDevicePixel(styleToUse.textDecorationThickness().resolve(styleToUse.computedFontSize(), styleToUse.metricsOfPrimaryFont()), deviceScaleFactor);
}

static inline float computedAutoTextDecorationThickness(const RenderStyle& styleToUse, float deviceScaleFactor)
{
    return ceilToDevicePixel(Style::TextDecorationThickness { CSS::Keyword::Auto { } }.resolve(styleToUse.computedFontSize(), styleToUse.metricsOfPrimaryFont()), deviceScaleFactor);
}

static inline float computedLinethroughCenter(const RenderStyle& styleToUse, float textDecorationThickness, float autoTextDecorationThickness)
{
    auto center = 2 * styleToUse.metricsOfPrimaryFont().ascent() / 3 + autoTextDecorationThickness / 2;
    return center - textDecorationThickness / 2;
}

static inline OptionSet<TextDecorationLine> computedTextDecorationType(const RenderStyle& style, const TextDecorationPainter::Styles& textDecorationStyles)
{
    auto textDecorations = style.textDecorationLineInEffect();
    textDecorations.add(TextDecorationPainter::textDecorationsInEffectForStyle(textDecorationStyles));
    return textDecorations;
}

static inline const RenderStyle& decoratingBoxStyleForInlineBox(const InlineIterator::InlineBox& inlineBox, bool isFirstLine)
{
    if (!inlineBox.isRootInlineBox())
        return inlineBox.style();
    // "When specified on or propagated to a block container that establishes an inline formatting context, the decorations are propagated to an anonymous
    // inline box that wraps all the in-flow inline-level children of the block container"
    // https://drafts.csswg.org/css-text-decor-4/#line-decoration
    // Sadly we don't have the concept of anonymous inline box for all inline-level chidren when content forces us to generate anonymous block containers.
    for (const RenderElement* ancestor = &inlineBox.renderer(); ancestor; ancestor = ancestor->parent()) {
        if (!ancestor->isAnonymous())
            return isFirstLine ? ancestor->firstLineStyle() : ancestor->style();
    }
    ASSERT_NOT_REACHED();
    return inlineBox.style();
}

static inline bool isDecoratingBoxForBackground(const InlineIterator::InlineBox& inlineBox, const RenderStyle& styleToUse)
{
    if (auto* element = inlineBox.renderer().element(); element && (is<HTMLAnchorElement>(*element) || element->hasTagName(HTMLNames::fontTag))) {
        // <font> and <a> are always considered decorating boxes.
        return true;
    }
    return styleToUse.textDecorationLine().containsAny({ TextDecorationLine::Underline, TextDecorationLine::Overline })
        || (inlineBox.isRootInlineBox() && styleToUse.textDecorationLineInEffect().containsAny({ TextDecorationLine::Underline, TextDecorationLine::Overline }));
}

void TextBoxPainter::collectDecoratingBoxesForBackgroundPainting(DecoratingBoxList& decoratingBoxList, const InlineIterator::TextBoxIterator& textBox, FloatPoint textBoxLocation, const TextDecorationPainter::Styles& overrideDecorationStyle)
{
    auto ancestorInlineBox = textBox->parentInlineBox();
    if (!ancestorInlineBox) {
        ASSERT_NOT_REACHED();
        return;
    }

    if (ancestorInlineBox->isRootInlineBox()) {
        decoratingBoxList.append({ ancestorInlineBox, decoratingBoxStyleForInlineBox(*ancestorInlineBox, m_isFirstLine), overrideDecorationStyle, textBoxLocation });
        return;
    }

    if (!textBox->isHorizontal()) {
        // FIXME: Vertical writing mode needs some coordinate space transformation for parent inline boxes as we rotate the content with m_paintRect (see ::paint)
        decoratingBoxList.append({ ancestorInlineBox, m_style, overrideDecorationStyle, textBoxLocation });
        return;
    }

    enum UseOverriderDecorationStyle : bool { No, Yes };
    auto appendIfIsDecoratingBoxForBackground = [&] (auto& inlineBox, auto useOverriderDecorationStyle) {
        auto& style = decoratingBoxStyleForInlineBox(*inlineBox, m_isFirstLine);

        auto computedDecorationStyle = [&] {
            return TextDecorationPainter::stylesForRenderer(inlineBox->renderer(), style.textDecorationLineInEffect(), m_isFirstLine);
        };
        if (!isDecoratingBoxForBackground(*inlineBox, style)) {
            // Some cases even non-decoration boxes may have some decoration pieces coming from the marked text (e.g. highlight).
            if (useOverriderDecorationStyle == UseOverriderDecorationStyle::No || overrideDecorationStyle == computedDecorationStyle())
                return;
        }

        auto borderAndPaddingBefore = !inlineBox->isRootInlineBox() ? inlineBox->renderer().borderAndPaddingBefore() : LayoutUnit(0_lu);
        decoratingBoxList.append({
            inlineBox,
            style,
            useOverriderDecorationStyle == UseOverriderDecorationStyle::Yes ? overrideDecorationStyle : computedDecorationStyle(),
            { textBoxLocation.x(), m_paintOffset.y() + inlineBox->logicalTop() + borderAndPaddingBefore }
        });
    };

    // FIXME: Figure out if the decoration styles coming from the styled marked text should be used only on the closest inline box (direct parent).
    appendIfIsDecoratingBoxForBackground(ancestorInlineBox, UseOverriderDecorationStyle::Yes);
    while (!ancestorInlineBox->isRootInlineBox()) {
        ancestorInlineBox = ancestorInlineBox->parentInlineBox();
        if (!ancestorInlineBox) {
            ASSERT_NOT_REACHED();
            break;
        }
        appendIfIsDecoratingBoxForBackground(ancestorInlineBox, UseOverriderDecorationStyle::No);
    }
}

void TextBoxPainter::paintBackgroundDecorations(TextDecorationPainter& decorationPainter, const StyledMarkedText& markedText, const FloatRect& textBoxPaintRect)
{
    if (m_isCombinedText)
        m_paintInfo.context().concatCTM(rotation(m_paintRect, RotationDirection::Clockwise));

    auto textRun = m_paintTextRun.subRun(markedText.startOffset, markedText.endOffset - markedText.startOffset);

    auto textBox = makeIterator();
    auto decoratingBoxList = DecoratingBoxList { };
    collectDecoratingBoxesForBackgroundPainting(decoratingBoxList, textBox, textBoxPaintRect.location(), markedText.style.textDecorationStyles);

    for (auto& decoratingBox : makeReversedRange(decoratingBoxList)) {
        auto computedTextDecorationType = WebCore::computedTextDecorationType(decoratingBox.style, decoratingBox.textDecorationStyles);
        auto computedBackgroundDecorationGeometry = [&] {
            auto textDecorationThickness = computedTextDecorationThickness(decoratingBox.style, m_document.deviceScaleFactor());
            auto underlineOffset = [&] {
                if (!computedTextDecorationType.contains(TextDecorationLine::Underline))
                    return 0.f;
                auto baseOffset = underlineOffsetForTextBoxPainting(*decoratingBox.inlineBox, decoratingBox.style);
                auto wavyOffset = decoratingBox.textDecorationStyles.underline.decorationStyle == TextDecorationStyle::Wavy ? wavyOffsetFromDecoration() : 0.f;
                return baseOffset + wavyOffset;
            };
            auto autoTextDecorationThickness = computedAutoTextDecorationThickness(decoratingBox.style, m_document.deviceScaleFactor());
            auto overlineOffset = [&] {
                if (!computedTextDecorationType.contains(TextDecorationLine::Overline))
                    return 0.f;
                auto baseOffset = overlineOffsetForTextBoxPainting(*decoratingBox.inlineBox, decoratingBox.style);
                baseOffset += (autoTextDecorationThickness - textDecorationThickness);
                auto wavyOffset = decoratingBox.textDecorationStyles.overline.decorationStyle == TextDecorationStyle::Wavy ? wavyOffsetFromDecoration() : 0.f;
                return baseOffset - wavyOffset;
            };

            return TextDecorationPainter::BackgroundDecorationGeometry {
                textOriginFromPaintRect(textBoxPaintRect),
                roundPointToDevicePixels(LayoutPoint { decoratingBox.location }, m_document.deviceScaleFactor(), m_paintTextRun.ltr()),
                textBoxPaintRect.width(),
                textDecorationThickness,
                underlineOffset(),
                overlineOffset(),
                computedLinethroughCenter(decoratingBox.style, textDecorationThickness, autoTextDecorationThickness),
                decoratingBox.style.metricsOfPrimaryFont().intAscent() + 2.f,
                wavyStrokeParameters(decoratingBox.style.computedFontSize())
            };
        };

        decorationPainter.paintBackgroundDecorations(m_style, textRun, computedBackgroundDecorationGeometry(), computedTextDecorationType, decoratingBox.textDecorationStyles);
    }

    if (m_isCombinedText)
        m_paintInfo.context().concatCTM(rotation(m_paintRect, RotationDirection::Counterclockwise));
}

static const RenderStyle& decoratingBoxStyle(const InlineIterator::TextBoxIterator& textBox)
{
    if (auto parentInlineBox = textBox->parentInlineBox())
        return parentInlineBox->style();
    ASSERT_NOT_REACHED();
    return textBox->style();
}

void TextBoxPainter::paintForegroundDecorations(TextDecorationPainter& decorationPainter, const StyledMarkedText& markedText, const FloatRect& textBoxPaintRect)
{
    auto textBox = makeIterator();
    auto& styleForDecoration = decoratingBoxStyle(textBox);
    auto computedTextDecorationType = [&] {
        auto textDecorations = styleForDecoration.textDecorationLineInEffect();
        textDecorations.add(TextDecorationPainter::textDecorationsInEffectForStyle(markedText.style.textDecorationStyles));
        return textDecorations;
    }();

    if (!computedTextDecorationType.contains(TextDecorationLine::LineThrough))
        return;

    if (m_isCombinedText)
        m_paintInfo.context().concatCTM(rotation(m_paintRect, RotationDirection::Clockwise));

    auto deviceScaleFactor = m_document.deviceScaleFactor();
    auto textDecorationThickness = computedTextDecorationThickness(styleForDecoration, deviceScaleFactor);
    auto linethroughCenter = computedLinethroughCenter(styleForDecoration, textDecorationThickness, computedAutoTextDecorationThickness(styleForDecoration, deviceScaleFactor));
    decorationPainter.paintForegroundDecorations({ textBoxPaintRect.location()
        , textBoxPaintRect.width()
        , textDecorationThickness
        , linethroughCenter
        , wavyStrokeParameters(styleForDecoration.computedFontSize()) }, markedText.style.textDecorationStyles);

    if (m_isCombinedText)
        m_paintInfo.context().concatCTM(rotation(m_paintRect, RotationDirection::Counterclockwise));
}

static FloatRoundedRect::Radii radiiForUnderline(const CompositionUnderline& underline, unsigned markedTextStartOffset, unsigned markedTextEndOffset)
{
    auto radii = FloatRoundedRect::Radii { 0 };

#if HAVE(REDESIGNED_TEXT_CURSOR)
    if (!redesignedTextCursorEnabled())
        return radii;

    if (underline.startOffset >= markedTextStartOffset) {
        radii.setTopLeft({ 1, 1 });
        radii.setBottomLeft({ 1, 1 });
    }

    if (underline.endOffset <= markedTextEndOffset) {
        radii.setTopRight({ 1, 1 });
        radii.setBottomRight({ 1, 1 });
    }
#else
    UNUSED_PARAM(underline);
    UNUSED_PARAM(markedTextStartOffset);
    UNUSED_PARAM(markedTextEndOffset);
#endif

    return radii;
}

#if HAVE(REDESIGNED_TEXT_CURSOR)
enum class TrimSide : bool {
    Left,
    Right,
};

static FloatRoundedRect::Radii trimRadii(const FloatRoundedRect::Radii& radii, TrimSide trimSide)
{
    switch (trimSide) {
    case TrimSide::Left:
        return { { }, radii.topRight(), { }, radii.bottomRight() };
    case TrimSide::Right:
        return { radii.topLeft(), { }, radii.bottomLeft(), { } };
    }
}

enum class SnapDirection : uint8_t {
    Left,
    Right,
    Both,
};

static FloatRect snapRectToDevicePixelsInDirection(const FloatRect& rect, float deviceScaleFactor, SnapDirection snapDirection)
{
    const auto layoutRect = LayoutRect { rect };
    switch (snapDirection) {
    case SnapDirection::Left:
        return snapRectToDevicePixelsWithWritingDirection(layoutRect, deviceScaleFactor, true);
    case SnapDirection::Right:
        return snapRectToDevicePixelsWithWritingDirection(layoutRect, deviceScaleFactor, false);
    case SnapDirection::Both:
        auto snappedRectLeft = snapRectToDevicePixelsWithWritingDirection(layoutRect, deviceScaleFactor, true);
        return snapRectToDevicePixelsWithWritingDirection(LayoutRect { snappedRectLeft }, deviceScaleFactor, false);
    }
}

enum class LayoutBoxLocation : uint8_t {
    OnlyBox,
    StartOfSequence,
    EndOfSequence,
    MiddleOfSequence,
    Unknown,
};

static LayoutBoxLocation layoutBoxSequenceLocation(const InlineIterator::BoxModernPath& textBox)
{
    auto isFirstForLayoutBox = textBox.box().isFirstForLayoutBox();
    auto isLastForLayoutBox = textBox.box().isLastForLayoutBox();
    if (isFirstForLayoutBox && isLastForLayoutBox)
        return LayoutBoxLocation::OnlyBox;
    if (isFirstForLayoutBox)
        return LayoutBoxLocation::StartOfSequence;
    if (isLastForLayoutBox)
        return LayoutBoxLocation::EndOfSequence;
    return LayoutBoxLocation::MiddleOfSequence;
}
#endif

void TextBoxPainter::fillCompositionUnderline(float start, float width, const CompositionUnderline& underline, const FloatRoundedRect::Radii& radii, bool hasLiveConversion) const
{
#if HAVE(REDESIGNED_TEXT_CURSOR)
    if (!redesignedTextCursorEnabled())
#endif
    {
        // Thick marked text underlines are 2px thick as long as there is room for the 2px line under the baseline.
        // All other marked text underlines are 1px thick.
        // If there's not enough space the underline will touch or overlap characters.
        int lineThickness = 1;
        int baseline = m_style.metricsOfPrimaryFont().intAscent();
        if (underline.thick && m_logicalRect.height() - baseline >= 2)
            lineThickness = 2;

        // We need to have some space between underlines of subsequent clauses, because some input methods do not use different underline styles for those.
        // We make each line shorter, which has a harmless side effect of shortening the first and last clauses, too.
        start += 1;
        width -= 2;

        auto underlineColor = underline.compositionUnderlineColor == CompositionUnderlineColor::TextColor ? m_style.visitedDependentColorWithColorFilter(CSSPropertyWebkitTextFillColor) : m_style.colorByApplyingColorFilter(underline.color);

        auto& context = m_paintInfo.context();
        context.setStrokeColor(underlineColor);
        context.setStrokeThickness(lineThickness);
        context.drawLineForText(FloatRect(m_paintRect.x() + start, m_paintRect.y() + m_logicalRect.height() - lineThickness, width, lineThickness), m_isPrinting);
        return;
    }

#if HAVE(REDESIGNED_TEXT_CURSOR)
    if (!underline.color.isVisible())
        return;

    // Thick marked text underlines are 2px thick as long as there is room for the 2px line under the baseline.
    // All other marked text underlines are 1px thick.
    // If there's not enough space the underline will touch or overlap characters.
    int lineThickness = 1;
    int baseline = m_style.metricsOfPrimaryFont().intAscent();
    if (m_logicalRect.height() - baseline >= 2)
        lineThickness = 2;

    auto underlineColor = [this] {
#if PLATFORM(MAC)
        auto cssColorValue = CSSValueAppleSystemControlAccent;
#else
        auto cssColorValue = CSSValueAppleSystemBlue;
#endif
        auto styleColorOptions = m_renderer.styleColorOptions();
        return RenderTheme::singleton().systemColor(cssColorValue, styleColorOptions | StyleColorOptions::UseSystemAppearance);
    }();

    if (!underline.thick && hasLiveConversion)
        underlineColor = underlineColor.colorWithAlpha(0.35);

    auto& context = m_paintInfo.context();
    context.setStrokeColor(underlineColor);
    context.setStrokeThickness(lineThickness);

    auto rect = FloatRect(m_paintRect.x() + start, m_paintRect.y() + m_logicalRect.height() - lineThickness, width, lineThickness);

    if (radii.isZero()) {
        context.drawLineForText(rect, m_isPrinting);
        return;
    }

    // We cannot directly draw rounded edges for every rect, since a single textbox path may be split up over multiple rects.
    // Drawing rounded edges unconditionally could then produce broken underlines between continuous rects.
    // As a mitigation, we consult the textbox path to understand the current rect's position in the textbox path.
    // If we're the only box in the path, then we fallback to unconditionally drawing rounded edges.
    // If not, we flatten out the right, left, or both edges depending on whether we're at the start, end, or middle of a path, respectively.

    auto deviceScaleFactor = m_document.deviceScaleFactor();

    switch (layoutBoxSequenceLocation(m_textBox)) {
    case LayoutBoxLocation::Unknown:
    case LayoutBoxLocation::OnlyBox: {
        context.fillRoundedRect(FloatRoundedRect { rect, radii }, underlineColor);
        return;
    }
    case LayoutBoxLocation::StartOfSequence: {
        auto snappedRectRight = snapRectToDevicePixelsInDirection(rect, deviceScaleFactor, SnapDirection::Right);
        context.fillRoundedRect(FloatRoundedRect { snappedRectRight, trimRadii(radii, TrimSide::Right) }, underlineColor);
        return;
    }
    case LayoutBoxLocation::EndOfSequence: {
        auto snappedRectLeft = snapRectToDevicePixelsInDirection(rect, deviceScaleFactor, SnapDirection::Left);
        context.fillRoundedRect(FloatRoundedRect { snappedRectLeft, trimRadii(radii, TrimSide::Left) }, underlineColor);
        return;
    }
    case LayoutBoxLocation::MiddleOfSequence: {
        auto snappedRectBoth = snapRectToDevicePixelsInDirection(rect, deviceScaleFactor, SnapDirection::Both);
        context.fillRect(snappedRectBoth, underlineColor);
        return;
    }
    }
    ASSERT_NOT_REACHED("Unexpected LayoutBoxLocation value, underline not drawn");
#else
    UNUSED_PARAM(radii);
    UNUSED_PARAM(hasLiveConversion);
#endif
}

void TextBoxPainter::paintCompositionUnderlines()
{        
    auto& underlines = m_renderer.frame().editor().customCompositionUnderlines();
    auto underlineCount = underlines.size();

    if (!underlineCount)
        return;

    auto hasLiveConversion = false;

    auto markedTextStartOffset = underlines[0].startOffset;
    auto markedTextEndOffset = underlines[0].endOffset;

    for (const auto& underline : underlines) {
        if (underline.thick)
            hasLiveConversion = true;

        if (underline.startOffset < markedTextStartOffset)
            markedTextStartOffset = underline.startOffset;

        if (underline.endOffset > markedTextEndOffset)
            markedTextEndOffset = underline.endOffset;
    }

    for (size_t i = 0; i < underlineCount; ++i) {
        auto& underline = underlines[i];
        if (underline.endOffset <= textBox().start()) {
            // Underline is completely before this run. This might be an underline that sits
            // before the first run we draw, or underlines that were within runs we skipped
            // due to truncation.
            continue;
        }

        if (underline.startOffset >= textBox().end())
            break; // Underline is completely after this run, bail. A later run will paint it.

        auto underlineRadii = radiiForUnderline(underline, markedTextStartOffset, markedTextEndOffset);

        // Underline intersects this run. Paint it.
        paintCompositionUnderline(underline, underlineRadii, hasLiveConversion);

        if (underline.endOffset > textBox().end())
            break; // Underline also runs into the next run. Bail now, no more marker advancement.
    }
}

static inline void mirrorRTLSegment(float logicalWidth, TextDirection direction, float& start, float width)
{
    if (direction == TextDirection::LTR)
        return;
    start = logicalWidth - width - start;
}

float TextBoxPainter::textPosition()
{
    // When computing the width of a text run, RenderBlock::computeInlineDirectionPositionsForLine() doesn't include the actual offset
    // from the containing block edge in its measurement. textPosition() should be consistent so the text are rendered in the same width.
    if (!m_logicalRect.x())
        return 0;
    return m_logicalRect.x() - makeIterator()->lineBox()->contentLogicalLeft();
}

void TextBoxPainter::paintCompositionUnderline(const CompositionUnderline& underline, const FloatRoundedRect::Radii& radii, bool hasLiveConversion)
{
    float start = 0; // start of line to draw, relative to tx
    float width = m_logicalRect.width(); // how much line to draw
    bool useWholeWidth = true;
    unsigned paintStart = textBox().start();
    unsigned paintEnd = textBox().end();
    if (paintStart <= underline.startOffset) {
        paintStart = underline.startOffset;
        useWholeWidth = false;
        start = m_renderer.width(textBox().start(), paintStart - textBox().start(), textPosition(), m_isFirstLine);
    }
    if (paintEnd != underline.endOffset) {
        paintEnd = std::min(paintEnd, (unsigned)underline.endOffset);
        useWholeWidth = false;
    }
    if (m_selectableRange.truncation) {
        paintEnd = std::min(paintEnd, textBox().start() + *m_selectableRange.truncation);
        useWholeWidth = false;
    }
    if (!useWholeWidth) {
        width = m_renderer.width(paintStart, paintEnd - paintStart, textPosition() + start, m_isFirstLine);
        mirrorRTLSegment(m_logicalRect.width(), textBox().direction(), start, width);
    }

    fillCompositionUnderline(start, width, underline, radii, hasLiveConversion);
}

void TextBoxPainter::paintPlatformDocumentMarkers()
{
    auto markedTexts = MarkedText::collectForDocumentMarkers(m_renderer, m_selectableRange, MarkedText::PaintPhase::Decoration);
    if (markedTexts.isEmpty())
        return;

    auto spellingErrorStyle = m_renderer.spellingErrorPseudoStyle();
    if (spellingErrorStyle && !spellingErrorStyle->textDecorationLineInEffect().isEmpty()) {
        markedTexts.removeAllMatching([] (auto&& markedText) {
            return markedText.type == MarkedText::Type::SpellingError;
        });
    }

    auto grammarErrorStyle = m_renderer.grammarErrorPseudoStyle();
    if (grammarErrorStyle && !grammarErrorStyle->textDecorationLineInEffect().isEmpty()) {
        markedTexts.removeAllMatching([] (auto&& markedText) {
            return markedText.type == MarkedText::Type::GrammarError;
        });
    }

    auto transparentContentMarkedTexts = MarkedText::collectForDraggedAndTransparentContent(DocumentMarkerType::TransparentContent, m_renderer, m_selectableRange);

    // Ensure the transparent content marked texts go first in the vector, so that they take precedence over
    // the other marked texts when being subdivided so that they do not get painted.
    Vector<MarkedText> allMarkedTexts;
    allMarkedTexts.appendVector(transparentContentMarkedTexts);
    allMarkedTexts.appendVector(markedTexts);

    for (auto& markedText : MarkedText::subdivide(allMarkedTexts, MarkedText::OverlapStrategy::Frontmost)) {
        switch (markedText.type) {
        case MarkedText::Type::DraggedContent:
        case MarkedText::Type::TransparentContent:
            continue;

        default:
            paintPlatformDocumentMarker(markedText);
            break;
        }
    }
}

#if ENABLE(WRITING_TOOLS)

constexpr Seconds writingToolsAnimationLoop = 10000_ms;
static void drawWritingToolsUnderline(GraphicsContext& context, const FloatRect& rect, IntSize frameSize)
{
    auto radius = rect.height() / 2.0;
    auto minX = rect.x();
    auto maxX = rect.maxX();
    auto minY = rect.y();
    auto maxY = rect.maxY();
    auto midY = (minY + maxY) / 2.0;

    auto frameX = frameSize.width();
    auto frameY = frameSize.height();

    constexpr auto redColor = SRGBA<uint8_t> { 227, 100, 136 };
    constexpr auto yellowColor = SRGBA<uint8_t> { 242, 225, 162 };
    constexpr auto purpleColor = SRGBA<uint8_t> { 154, 109, 209 };

    auto animationProgress = (MonotonicTime::now() % writingToolsAnimationLoop).value() / 10;

    auto xOffset = frameX * fmod(animationProgress + midY / frameY, 1.0);
    constexpr std::array colorList { purpleColor, redColor, yellowColor, redColor, purpleColor, purpleColor, redColor, yellowColor, redColor, purpleColor };

    Ref gradient = Gradient::create(Gradient::LinearData { FloatPoint(0 - xOffset, 0), FloatPoint(frameX * 2 - xOffset, frameY) }, { ColorInterpolationMethod::SRGB { }, AlphaPremultiplication::Unpremultiplied });

    auto colorStop = 0.f;
    auto colorIncrement = 1.0 / colorList.size();
    for (auto color : colorList) {
        gradient->addColorStop({ colorStop, color });
        colorStop += colorIncrement;
    }

    context.save();
    context.setFillGradient(WTFMove(gradient));

    Path path;
    path.moveTo(FloatPoint(minX + radius, maxY));
    path.addArc(FloatPoint(minX + radius, midY), radius, piOverTwoDouble, 3 * piOverTwoDouble, RotationDirection::Clockwise);
    path.addLineTo(FloatPoint(maxX - radius, minY));
    path.addArc(FloatPoint(maxX - radius, midY), radius, 3 * piOverTwoDouble, piOverTwoDouble, RotationDirection::Clockwise);

    context.fillPath(path);
    context.restore();
}

#endif // ENABLE(WRITING_TOOLS)

void TextBoxPainter::paintPlatformDocumentMarker(const MarkedText& markedText)
{
    // Never print document markers (rdar://5327887)
    if (m_document.printing())
        return;

    auto bounds = calculateDocumentMarkerBounds(makeIterator(), markedText);
    bounds.moveBy(m_paintRect.location());

#if ENABLE(WRITING_TOOLS)
    if (markedText.type == MarkedText::Type::WritingToolsTextSuggestion) {
        drawWritingToolsUnderline(m_paintInfo.context(), bounds,  m_renderer.frame().view()->size());
        return;
    }
#endif

    auto lineStyleMode = [&] {
        switch (markedText.type) {
        case MarkedText::Type::SpellingError:
            return DocumentMarkerLineStyleMode::Spelling;
        case MarkedText::Type::GrammarError:
            return DocumentMarkerLineStyleMode::Grammar;
        case MarkedText::Type::Correction:
            return DocumentMarkerLineStyleMode::AutocorrectionReplacement;
        case MarkedText::Type::DictationAlternatives:
            return DocumentMarkerLineStyleMode::DictationAlternatives;
#if PLATFORM(IOS_FAMILY)
        case MarkedText::Type::DictationPhraseWithAlternatives:
            // FIXME: Rename DocumentMarkerLineStyle::TextCheckingDictationPhraseWithAlternatives and remove the PLATFORM(IOS_FAMILY)-guard.
            return DocumentMarkerLineStyleMode::TextCheckingDictationPhraseWithAlternatives;
#endif
        default:
            ASSERT_NOT_REACHED();
            return DocumentMarkerLineStyleMode::Spelling;
        }
    }();

    auto lineStyleColor = RenderTheme::singleton().documentMarkerLineColor(m_renderer, lineStyleMode);
    if (auto* marker = markedText.marker)
        lineStyleColor = lineStyleColor.colorWithAlphaMultipliedBy(marker->opacity());

    m_paintInfo.context().drawDotsForDocumentMarker(bounds, { lineStyleMode, lineStyleColor });
}

FloatRect TextBoxPainter::computePaintRect(const LayoutPoint& paintOffset)
{
    FloatPoint localPaintOffset(paintOffset);
    if (writingMode().isVertical()) {
        localPaintOffset.move(0, -m_logicalRect.height());
        if (writingMode().isLineOverLeft())
            localPaintOffset.move(m_logicalRect.height(), m_logicalRect.width());
    }

    auto visualRect = textBox().visualRectIgnoringBlockDirection();
    textBox().formattingContextRoot().flipForWritingMode(visualRect);

    auto boxOrigin = visualRect.location();
    boxOrigin.moveBy(localPaintOffset);

    return { boxOrigin, FloatSize(m_logicalRect.width(), m_logicalRect.height()) };
}

FloatRect calculateDocumentMarkerBounds(const InlineIterator::TextBoxIterator& textBox, const MarkedText& markedText)
{
    auto& font = textBox->fontCascade();
    auto [y, height] = DocumentMarkerController::markerYPositionAndHeightForFont(font);

    // Avoid measuring the text when the entire line box is selected as an optimization.
    if (markedText.startOffset || markedText.endOffset != textBox->selectableRange().clamp(textBox->end())) {
        auto run = textBox->textRun();
        auto selectionRect = LayoutRect { 0_lu, y, 0_lu, height };
        font.adjustSelectionRectForText(textBox->renderer().canUseSimplifiedTextMeasuring().value_or(false), run, selectionRect, markedText.startOffset, markedText.endOffset);
        return selectionRect;
    }

    return FloatRect(0, y, textBox->logicalWidth(), height);
}

bool TextBoxPainter::computeHaveSelection() const
{
    if (m_isPrinting || m_paintInfo.phase == PaintPhase::TextClip)
        return false;

    return m_renderer.view().selection().highlightStateForTextBox(m_renderer, m_selectableRange) != RenderObject::HighlightState::None;
}

const FontCascade& TextBoxPainter::fontCascade() const
{
    if (m_isCombinedText)
        return downcast<RenderCombineText>(m_renderer).textCombineFont();

    return m_style.fontCascade();
}

FloatPoint TextBoxPainter::textOriginFromPaintRect(const FloatRect& paintRect) const
{
    FloatPoint textOrigin { paintRect.x(), paintRect.y() + fontCascade().metricsOfPrimaryFont().intAscent() };

    if (m_isCombinedText) {
        if (auto newOrigin = downcast<RenderCombineText>(m_renderer).computeTextOrigin(paintRect))
            textOrigin = newOrigin.value();
    }

    auto writingMode = textBox().writingMode();
    if (writingMode.isHorizontal())
        textOrigin.setY(roundToDevicePixel(LayoutUnit { textOrigin.y() }, m_document.deviceScaleFactor()));
    else
        textOrigin.setX(roundToDevicePixel(LayoutUnit { textOrigin.x() }, m_document.deviceScaleFactor()));

    return textOrigin;
}

}
