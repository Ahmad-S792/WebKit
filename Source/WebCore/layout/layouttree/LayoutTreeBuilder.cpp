/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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
#include "LayoutTreeBuilder.h"

#include "CachedImage.h"
#include "HTMLNames.h"
#include "HTMLParserIdioms.h"
#include "HTMLTableCellElement.h"
#include "HTMLTableColElement.h"
#include "HTMLTableElement.h"
#include "InlineDisplayContent.h"
#include "LayoutBox.h"
#include "LayoutBoxGeometry.h"
#include "LayoutChildIterator.h"
#include "LayoutContext.h"
#include "LayoutElementBox.h"
#include "LayoutInitialContainingBlock.h"
#include "LayoutInlineTextBox.h"
#include "LayoutPhase.h"
#include "LayoutSize.h"
#include "LayoutState.h"
#include "PathOperation.h"
#include "RenderBlock.h"
#include "RenderBox.h"
#include "RenderChildIterator.h"
#include "RenderCombineText.h"
#include "RenderElementInlines.h"
#include "RenderImage.h"
#include "RenderInline.h"
#include "RenderLineBreak.h"
#include "RenderObjectInlines.h"
#include "RenderStyleSetters.h"
#include "RenderTable.h"
#include "RenderTableCaption.h"
#include "RenderTableCell.h"
#include "RenderView.h"
#include "TextUtil.h"
#include "WidthIterator.h"
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/TextStream.h>

namespace WebCore {
namespace Layout {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(LayoutTree);
LayoutTree::LayoutTree(std::unique_ptr<ElementBox> root)
    : m_root(WTFMove(root))
{
}

template<class BoxType>
static BoxType& appendChild(ElementBox& parent, std::unique_ptr<BoxType> newChild)
{
    auto& box = *newChild;
    parent.appendChild(makeUniqueRefFromNonNullUniquePtr(WTFMove(newChild)));
    return box;
}

static std::optional<LayoutSize> accumulatedOffsetForInFlowPositionedContinuation(const RenderBox& block)
{
    // FIXE: This is a workaround of the continuation logic when the relatively positioned parent inline box
    // becomes a sibling box of this block and only reachable through the continuation link which we don't have here.
    if (!block.isAnonymous() || !block.isInFlowPositioned() || !block.isContinuation())
        return { };
    return block.relativePositionOffset();
}

template<typename CharacterType>
static bool canUseSimplifiedTextMeasuringForCharacters(std::span<const CharacterType> characters, const FontCascade& fontCascade, bool whitespaceIsCollapsed)
{
    Ref primaryFont = fontCascade.primaryFont();
    for (auto character : characters) {
        if (!fontCascade.canUseSimplifiedTextMeasuring(character, AutoVariant, whitespaceIsCollapsed, primaryFont))
            return false;
    }
    return true;
}

static bool canUseSimplifiedTextMeasuring(StringView content, const FontCascade& fontCascade, bool whitespaceIsCollapsed)
{
    if (fontCascade.codePath(TextRun(content)) == FontCascade::CodePath::Complex)
        return false;

    if (fontCascade.wordSpacing() || fontCascade.letterSpacing())
        return false;

    if (content.is8Bit())
        return canUseSimplifiedTextMeasuringForCharacters(content.span8(), fontCascade, whitespaceIsCollapsed);
    return canUseSimplifiedTextMeasuringForCharacters(content.span16(), fontCascade, whitespaceIsCollapsed);
}

std::unique_ptr<Layout::LayoutTree> TreeBuilder::buildLayoutTree(const RenderView& renderView)
{
    PhaseScope scope(Phase::Type::TreeBuilding);

    auto rootStyle = RenderStyle::clone(renderView.style());
    rootStyle.setLogicalWidth(Style::PreferredSize::Fixed { renderView.width() });
    rootStyle.setLogicalHeight(Style::PreferredSize::Fixed { renderView.height() });

    auto rootLayoutBox = makeUnique<InitialContainingBlock>(WTFMove(rootStyle));
    TreeBuilder().buildSubTree(renderView, *rootLayoutBox);

    return makeUnique<LayoutTree>(WTFMove(rootLayoutBox));
}

TreeBuilder::TreeBuilder()
{
}

std::unique_ptr<Box> TreeBuilder::createReplacedBox(Box::ElementAttributes elementAttributes, ElementBox::ReplacedAttributes&& replacedAttributes, RenderStyle&& style)
{
    return makeUnique<ElementBox>(WTFMove(elementAttributes), WTFMove(replacedAttributes), WTFMove(style));
}

std::unique_ptr<Box> TreeBuilder::createTextBox(String text, bool isCombined, bool canUseSimplifiedTextMeasuring, bool canUseSimpleFontCodePath, bool hasPositionDependentContentWidth, bool hasStrongDirectionalityContent, RenderStyle&& style)
{
    auto contentCharacteristic = OptionSet<Layout::InlineTextBox::ContentCharacteristic> { };
    if (canUseSimpleFontCodePath)
        contentCharacteristic.add(Layout::InlineTextBox::ContentCharacteristic::CanUseSimpleFontCodepath);
    if (canUseSimplifiedTextMeasuring)
        contentCharacteristic.add(Layout::InlineTextBox::ContentCharacteristic::CanUseSimplifiedContentMeasuring);
    if (hasPositionDependentContentWidth)
        contentCharacteristic.add(Layout::InlineTextBox::ContentCharacteristic::HasPositionDependentContentWidth);
    if (hasStrongDirectionalityContent)
        contentCharacteristic.add(Layout::InlineTextBox::ContentCharacteristic::HasStrongDirectionalityContent);
    return makeUnique<InlineTextBox>(text, isCombined, contentCharacteristic, WTFMove(style));
}

std::unique_ptr<ElementBox> TreeBuilder::createContainer(Box::ElementAttributes elementAttributes, RenderStyle&& style)
{
    return makeUnique<ElementBox>(WTFMove(elementAttributes), WTFMove(style));
}

std::unique_ptr<Box> TreeBuilder::createLayoutBox(const ElementBox& parentContainer, const RenderObject& childRenderer)
{
    auto elementAttributes = [] (const RenderElement& renderer) -> Box::ElementAttributes {
        auto isAnonymous = renderer.isAnonymous() ? Box::IsAnonymous::Yes : Box::IsAnonymous::No;
        if (renderer.isDocumentElementRenderer())
            return { Box::NodeType::DocumentElement, isAnonymous };
        if (auto* renderLineBreak = dynamicDowncast<RenderLineBreak>(renderer))
            return { renderLineBreak->isWBR() ? Box::NodeType::WordBreakOpportunity : Box::NodeType::LineBreak, isAnonymous };
        if (auto* element = renderer.element()) {
            if (element->hasTagName(HTMLNames::bodyTag))
                return { Box::NodeType::Body, isAnonymous };
            if (element->hasTagName(HTMLNames::imgTag))
                return { Box::NodeType::Image, isAnonymous };
            if (element->hasTagName(HTMLNames::iframeTag))
                return { Box::NodeType::IFrame, isAnonymous };
            return { Box::NodeType::GenericElement, isAnonymous };
        }
        return { Box::NodeType::GenericElement, Box::IsAnonymous::Yes };
    };

    std::unique_ptr<Box> childLayoutBox = nullptr;
    if (auto* textRenderer = dynamicDowncast<RenderText>(childRenderer)) {
        // RenderText::text() has already applied text-transform and text-security properties.
        String text = textRenderer->text();
        auto useSimplifiedTextMeasuring = canUseSimplifiedTextMeasuring(text, parentContainer.style().fontCascade(), parentContainer.style().collapseWhiteSpace());
        auto hasPositionDependentContentWidth = textRenderer->hasPositionDependentContentWidth();
        if (!hasPositionDependentContentWidth) {
            hasPositionDependentContentWidth = TextUtil::hasPositionDependentContentWidth(text);
            const_cast<RenderText*>(textRenderer)->setHasPositionDependentContentWidth(*hasPositionDependentContentWidth);
        }
        auto hasStrongDirectionalityContent = textRenderer->hasStrongDirectionalityContent();
        if (!hasStrongDirectionalityContent) {
            hasStrongDirectionalityContent = TextUtil::containsStrongDirectionalityText(text);
            const_cast<RenderText*>(textRenderer)->setHasStrongDirectionalityContent(*hasStrongDirectionalityContent);
        }
        if (parentContainer.style().display() == DisplayType::Inline)
            childLayoutBox = createTextBox(text, is<RenderCombineText>(childRenderer), useSimplifiedTextMeasuring, textRenderer->canUseSimpleFontCodePath(), *hasPositionDependentContentWidth, *hasStrongDirectionalityContent, RenderStyle::clone(parentContainer.style()));
        else
            childLayoutBox = createTextBox(text, is<RenderCombineText>(childRenderer), useSimplifiedTextMeasuring, textRenderer->canUseSimpleFontCodePath(), *hasPositionDependentContentWidth, *hasStrongDirectionalityContent, RenderStyle::createAnonymousStyleWithDisplay(parentContainer.style(), DisplayType::Inline));
    } else {
        auto& renderer = downcast<RenderElement>(childRenderer);
        auto displayType = renderer.style().display();

        auto clonedStyle = RenderStyle::clone(renderer.style());

        if (is<RenderLineBreak>(renderer)) {
            clonedStyle.setDisplay(DisplayType::Inline);
            clonedStyle.setFloating(Float::None);
            clonedStyle.setPosition(PositionType::Static);
            childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
        } else if (is<RenderTable>(renderer)) {
            // Construct the principal table wrapper box (and not the table box itself).
            // The computed values of properties 'position', 'float', 'margin-*', 'top', 'right', 'bottom', and 'left' on the table element
            // are used on the table wrapper box and not the table box; all other values of non-inheritable properties are used
            // on the table box and not the table wrapper box.
            auto tableWrapperBoxStyle = RenderStyle::createAnonymousStyleWithDisplay(parentContainer.style(), renderer.style().display() == DisplayType::Table ? DisplayType::Block : DisplayType::Inline);
            tableWrapperBoxStyle.setPosition(renderer.style().position());
            tableWrapperBoxStyle.setFloating(renderer.style().floating());

            tableWrapperBoxStyle.setInsetBox(Style::InsetBox { renderer.style().insetBox() });
            tableWrapperBoxStyle.setMarginBox(Style::MarginBox { renderer.style().marginBox() });

            childLayoutBox = createContainer(Box::ElementAttributes { Box::NodeType::TableWrapperBox, Box::IsAnonymous::Yes }, WTFMove(tableWrapperBoxStyle));
        } else if (auto* replacedRenderer = dynamicDowncast<RenderReplaced>(renderer)) {
            auto replacedAttributes = ElementBox::ReplacedAttributes {
                replacedRenderer->intrinsicSize()
            };
            if (auto* imageRenderer = dynamicDowncast<RenderImage>(*replacedRenderer)) {
                if (imageRenderer->shouldDisplayBrokenImageIcon())
                    replacedAttributes.intrinsicRatio = 1;
                if (imageRenderer->cachedImage())
                    replacedAttributes.cachedImage = imageRenderer->cachedImage();
            }
            childLayoutBox = createReplacedBox(elementAttributes(renderer), WTFMove(replacedAttributes), WTFMove(clonedStyle));
        } else {
            if (displayType == DisplayType::Block) {
                if (auto offset = accumulatedOffsetForInFlowPositionedContinuation(downcast<RenderBox>(renderer))) {
                    clonedStyle.setTop(Style::InsetEdge::Fixed { offset->height() });
                    clonedStyle.setLeft(Style::InsetEdge::Fixed { offset->width() });
                    childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
                } else
                    childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
            } else if (displayType == DisplayType::Flex)
                childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
            else if (displayType == DisplayType::Inline)
                childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
            else if (displayType == DisplayType::InlineBlock)
                childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
            else if (displayType == DisplayType::TableCaption || displayType == DisplayType::TableCell) {
                childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
            } else if (displayType == DisplayType::TableRowGroup || displayType == DisplayType::TableHeaderGroup || displayType == DisplayType::TableFooterGroup
                || displayType == DisplayType::TableRow || displayType == DisplayType::TableColumnGroup) {
                childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
            } else if (displayType == DisplayType::TableColumn) {
                childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
                auto& tableColElement = static_cast<HTMLTableColElement&>(*renderer.element());
                auto columnWidth = tableColElement.width();
                if (!columnWidth.isEmpty())
                    childLayoutBox->setColumnWidth(parseHTMLInteger(columnWidth).value_or(0));
                if (tableColElement.span() > 1)
                    childLayoutBox->setColumnSpan(tableColElement.span());
            } else {
                ASSERT_NOT_IMPLEMENTED_YET();
                // Let's fall back to a regular block level container when the renderer type is not yet supported.
                clonedStyle.setDisplay(DisplayType::Block);
                childLayoutBox = createContainer(elementAttributes(renderer), WTFMove(clonedStyle));
            }
        }

        if (is<RenderTableCell>(renderer)) {
            auto* tableCellElement = renderer.element();
            if (auto* cellElement = dynamicDowncast<HTMLTableCellElement>(tableCellElement)) {
                auto rowSpan = cellElement->rowSpan();
                if (rowSpan > 1)
                    childLayoutBox->setRowSpan(rowSpan);
                auto columnSpan = cellElement->colSpan();
                if (columnSpan > 1)
                    childLayoutBox->setColumnSpan(columnSpan);
            }
        }
    }
    return childLayoutBox;
}

void TreeBuilder::buildTableStructure(const RenderTable& tableRenderer, ElementBox& tableWrapperBox)
{
    // Create caption and table box.
    auto* tableChild = tableRenderer.firstChild();
    while (is<RenderTableCaption>(tableChild)) {
        auto& captionRenderer = *tableChild;
        auto newCaptionBox = createLayoutBox(tableWrapperBox, captionRenderer);
        auto& captionBox = appendChild(tableWrapperBox, WTFMove(newCaptionBox));
        auto& captionContainer = downcast<ElementBox>(captionBox);
        buildSubTree(downcast<RenderElement>(captionRenderer), captionContainer);
        tableChild = tableChild->nextSibling();
    }

    auto tableBoxStyle = RenderStyle::clone(tableRenderer.style());
    tableBoxStyle.setPosition(PositionType::Static);
    tableBoxStyle.setFloating(Float::None);
    tableBoxStyle.resetMargin();
    // FIXME: Figure out where the spec says table width is like box-sizing: border-box;
    if (is<HTMLTableElement>(tableRenderer.element()))
        tableBoxStyle.setBoxSizing(BoxSizing::BorderBox);
    auto isAnonymous = tableRenderer.isAnonymous() ? Box::IsAnonymous::Yes : Box::IsAnonymous::No;
    auto newTableBox = createContainer(Box::ElementAttributes { Box::NodeType::TableBox, isAnonymous }, WTFMove(tableBoxStyle));
    auto& tableBox = appendChild(tableWrapperBox, WTFMove(newTableBox));
    auto* sectionRenderer = tableChild;
    while (sectionRenderer) {
        auto& sectionBox = appendChild(tableBox, createLayoutBox(tableBox, *sectionRenderer));
        auto& sectionContainer = downcast<ElementBox>(sectionBox);
        buildSubTree(downcast<RenderElement>(*sectionRenderer), sectionContainer);
        sectionRenderer = sectionRenderer->nextSibling();
    }
    auto addMissingTableCells = [&] (auto& tableBody) {
        // A "missing cell" is a cell in the row/column grid that is not occupied by an element or pseudo-element.
        // Missing cells are rendered as if an anonymous table-cell box occupied their position in the grid.

        // Find the max number of columns and fill in the gaps.
        size_t maximumColumns = 0;
        size_t currentRow = 0;
        Vector<size_t> numberOfCellsPerRow;
        for (auto& rowBox : childrenOfType<ElementBox>(tableBody)) {
            if (numberOfCellsPerRow.size() <= currentRow) {
                // Ensure we always have a vector entry for the current row -even when the row is empty.
                numberOfCellsPerRow.append({ });
            }
            for (auto& cellBox : childrenOfType<ElementBox>(rowBox)) {
                auto numberOfSpannedColumns = cellBox.columnSpan();
                for (size_t rowSpan = 0; rowSpan < cellBox.rowSpan(); ++rowSpan) {
                    auto rowIndexWithSpan = currentRow + rowSpan;
                    if (numberOfCellsPerRow.size() <= rowIndexWithSpan) {
                        // This is where we advance from the current row by having a row spanner.
                        numberOfCellsPerRow.append(numberOfSpannedColumns);
                        continue;
                    }
                    numberOfCellsPerRow[rowIndexWithSpan] += numberOfSpannedColumns;
                }
            }
            maximumColumns = std::max(maximumColumns, numberOfCellsPerRow[currentRow]);
            ++currentRow;
        }
        // Fill in the gaps.
        size_t rowIndex = 0;
        for (auto& rowBox : childrenOfType<ElementBox>(tableBody)) {
            ASSERT(maximumColumns >= numberOfCellsPerRow[rowIndex]);
            auto numberOfMissingCells = maximumColumns - numberOfCellsPerRow[rowIndex++];
            for (size_t i = 0; i < numberOfMissingCells; ++i)
                appendChild(const_cast<ElementBox&>(rowBox), createContainer({ }, RenderStyle::createAnonymousStyleWithDisplay(rowBox.style(), DisplayType::TableCell)));
        }
    };

    for (auto& section : childrenOfType<ElementBox>(tableBox)) {
        // FIXME: Check if headers and footers need the same treatment.
        if (!section.isTableBody())
            continue;
        addMissingTableCells(section);
    }
}

void TreeBuilder::buildSubTree(const RenderElement& parentRenderer, ElementBox& parentContainer)
{
    for (auto& childRenderer : childrenOfType<RenderObject>(parentRenderer)) {
        auto& childLayoutBox = appendChild(parentContainer, createLayoutBox(parentContainer, childRenderer));
        if (childLayoutBox.isTableWrapperBox())
            buildTableStructure(downcast<RenderTable>(childRenderer), downcast<ElementBox>(childLayoutBox));
        else if (auto* elementBox = dynamicDowncast<ElementBox>(childLayoutBox))
            buildSubTree(downcast<RenderElement>(childRenderer), *elementBox);
    }
}

#if ENABLE(TREE_DEBUGGING)
void showInlineTreeAndRuns(TextStream& stream, const LayoutState& layoutState, const ElementBox& inlineFormattingRoot, size_t depth)
{
    UNUSED_PARAM(layoutState);
    UNUSED_PARAM(inlineFormattingRoot);
    // FIXME: Populate inline display content.
    auto lines = InlineDisplay::Lines { };
    auto boxes = InlineDisplay::Boxes { };

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        auto addSpacing = [&] {
            size_t printedCharacters = 0;
            stream << "-------- --";
            while (++printedCharacters <= depth * 2)
                stream << " ";

        };
        addSpacing();
        auto& line = lines[lineIndex];
        auto& lineBoxRect = line.lineBoxRect();
        stream << "line at (" << lineBoxRect.x() << "," << lineBoxRect.y() << ") size (" << lineBoxRect.width() << "x" << lineBoxRect.height() << ") baseline (" << line.baseline() << ") enclosing top (" << line.enclosingContentLogicalTop() << ") bottom (" << line.enclosingContentLogicalBottom() << ")";
        stream.nextLine();

        addSpacing();
        stream << "  Inline level boxes:";
        stream.nextLine();

        auto outputInlineLevelBox = [&](const auto& inlineLevelBox) {
            addSpacing();
            stream << "    ";
            auto rect = inlineLevelBox.visualRectIgnoringBlockDirection();
            auto& layoutBox = inlineLevelBox.layoutBox();
            if (layoutBox.isAtomicInlineBox())
                stream << "Atomic inline box";
            else if (layoutBox.isLineBreakBox())
                stream << "Line break box";
            else if (layoutBox.isInlineBox())
                stream << "Inline box";
            else
                stream << "Generic inline level box";
            stream
                << " at (" << rect.x() << "," << rect.y() << ")"
                << " size (" << rect.width() << "x" << rect.height() << ")";
            stream.nextLine();
        };
        for (auto& box : boxes) {
            if (box.lineIndex() != lineIndex)
                continue;
            if (!box.layoutBox().isInlineLevelBox())
                continue;
            outputInlineLevelBox(box);
        }

        addSpacing();
        stream << "  Runs:";
        stream.nextLine();
        for (auto& box : boxes) {
            if (box.lineIndex() != lineIndex)
                continue;
            addSpacing();
            stream << "    ";
            if (box.isTextOrSoftLineBreak())
                stream << "text box";
            else
                stream << "box box";
            stream << " at (" << box.left() << "," << box.top() << ") size " << box.width() << "x" << box.height();
            if (box.isTextOrSoftLineBreak())
                stream << " box(" << box.text().start() << ", " << box.text().end() << ")";
            stream.nextLine();
        }

    }
}

static void outputLayoutBox(TextStream& stream, const Box& layoutBox, const BoxGeometry* boxGeometry, unsigned depth)
{
    unsigned printedCharacters = 0;
    while (++printedCharacters <= depth * 2)
        stream << " ";

    if (layoutBox.isFloatingPositioned())
        stream << "[float] ";

    if (is<InitialContainingBlock>(layoutBox))
        stream << "Initial containing block";
    else if (layoutBox.isDocumentBox())
        stream << "HTML";
    else if (layoutBox.isBodyBox())
        stream << "BODY";
    else if (layoutBox.isTableWrapperBox())
        stream << "TABLE wrapper box";
    else if (layoutBox.isTableBox())
        stream << "TABLE";
    else if (layoutBox.isTableCaption())
        stream << "CAPTION";
    else if (layoutBox.isTableHeader())
        stream << "THEAD";
    else if (layoutBox.isTableBody())
        stream << "TBODY";
    else if (layoutBox.isTableFooter())
        stream << "TFOOT";
    else if (layoutBox.isTableColumnGroup())
        stream << "COL GROUP";
    else if (layoutBox.isTableColumn())
        stream << "COL";
    else if (layoutBox.isTableCell())
        stream << "TD";
    else if (layoutBox.isTableRow())
        stream << "TR";
    else if (layoutBox.isFlexBox())
        stream << "Flex box";
    else if (layoutBox.isFlexItem())
        stream << "Flex item";
    else if (layoutBox.isInlineLevelBox()) {
        if (layoutBox.isAnonymous())
            stream << "anonymous inline box";
        else if (layoutBox.isInlineBlockBox())
            stream << "inline-block box";
        else if (layoutBox.isLineBreakBox())
            stream << (layoutBox.isWordBreakOpportunity() ? "word break opportunity" : "line break");
        else if (layoutBox.isAtomicInlineBox())
            stream << "atomic inline box";
        else if (layoutBox.isReplacedBox())
            stream << "replaced inline box";
        else if (layoutBox.isInlineBox())
            stream << "inline box";
        else
            stream << "other inline level box";
    } else if (layoutBox.isBlockLevelBox())
        stream << "block box";
    else
        stream << "unknown box";

    if (boxGeometry) {
        auto borderBox = BoxGeometry::borderBoxRect(*boxGeometry);
        stream << " at (" << borderBox.left() << "," << borderBox.top() << ") size " << borderBox.width() << "x" << borderBox.height();
    }
    stream << " (" << &layoutBox << ")";
    if (auto* inlineTextBox = dynamicDowncast<InlineTextBox>(layoutBox)) {
        auto textContent = inlineTextBox->content();
        stream << " length->(" << textContent.length() << ")";

        textContent = makeStringByReplacingAll(textContent, '\\', "\\\\"_s);
        textContent = makeStringByReplacingAll(textContent, '\n', "\\n"_s);

        const size_t maxPrintedLength = 80;
        if (textContent.length() > maxPrintedLength) {
            auto substring = StringView(textContent).left(maxPrintedLength);
            stream << " \"" << substring.utf8().data() << "\"...";
        } else
            stream << " \"" << textContent.utf8().data() << "\"";
    }
    stream.nextLine();
}

static void outputLayoutTree(const LayoutState* layoutState, TextStream& stream, const ElementBox& rootContainer, unsigned depth)
{
    for (auto& child : childrenOfType<Box>(rootContainer)) {
        if (layoutState) {
            // Not all boxes generate display boxes.
            if (layoutState->hasBoxGeometry(child))
                outputLayoutBox(stream, child, &layoutState->geometryForBox(child), depth);
            else
                outputLayoutBox(stream, child, nullptr, depth);
            if (child.establishesInlineFormattingContext())
                showInlineTreeAndRuns(stream, *layoutState, downcast<ElementBox>(child), depth + 1);
        } else
            outputLayoutBox(stream, child, nullptr, depth);

        if (auto* elementBox = dynamicDowncast<ElementBox>(child))
            outputLayoutTree(layoutState, stream, *elementBox, depth + 1);
    }
}

String layoutTreeAsText(const InitialContainingBlock& initialContainingBlock, const LayoutState* layoutState)
{
    TextStream stream(TextStream::LineMode::MultipleLine, TextStream::Formatting::SVGStyleRect);

    outputLayoutBox(stream, initialContainingBlock, layoutState ? &layoutState->geometryForBox(initialContainingBlock) : nullptr, 0);
    outputLayoutTree(layoutState, stream, initialContainingBlock, 1);
    
    return stream.release();
}

void showLayoutTree(const InitialContainingBlock& initialContainingBlock, const LayoutState* layoutState)
{
    auto treeAsText = layoutTreeAsText(initialContainingBlock, layoutState);
    WTFLogAlways("%s", treeAsText.utf8().data());
}

void showLayoutTree(const InitialContainingBlock& initialContainingBlock)
{
    showLayoutTree(initialContainingBlock, nullptr);
}

void printLayoutTreeForLiveDocuments()
{
    for (auto& document : Document::allDocuments()) {
        if (!document->renderView())
            continue;
        if (document->frame() && document->frame()->isMainFrame())
            fprintf(stderr, "----------------------main frame--------------------------\n");
        SAFE_FPRINTF(stderr, "%s\n", document->url().string().utf8());
        // FIXME: Need to find a way to output geometry without layout context.
        auto& renderView = *document->renderView();
        auto layoutTree = TreeBuilder::buildLayoutTree(renderView);
        auto layoutState = LayoutState { document, layoutTree->root(), Layout::LayoutState::Type::Secondary, { }, { }, { } };

        LayoutContext(layoutState).layout(renderView.size());
        showLayoutTree(downcast<InitialContainingBlock>(layoutState.root()), &layoutState);
    }
}
#endif

}
}
