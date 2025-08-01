/*
 * Copyright (C) 2005-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2025 Samuel Weinig <sam@webkit.org>
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

#import "config.h"
#import "RenderThemeIOS.h"

#if PLATFORM(IOS_FAMILY)

#import "ARKitBadgeSystemImage.h"
#import "BitmapImage.h"
#import "BorderShape.h"
#import "CSSPrimitiveValue.h"
#import "CSSToLengthConversionData.h"
#import "CSSValueKey.h"
#import "CSSValueKeywords.h"
#import "ColorBlending.h"
#import "ColorCocoa.h"
#import "ColorTypes.h"
#import "DateComponents.h"
#import "DeprecatedGlobalSettings.h"
#import "Document.h"
#import "DrawGlyphsRecorder.h"
#import "File.h"
#import "FloatRoundedRect.h"
#import "FontCache.h"
#import "FontCacheCoreText.h"
#import "FontCascade.h"
#import "FrameSelection.h"
#import "GeometryUtilities.h"
#import "Gradient.h"
#import "GraphicsContext.h"
#import "GraphicsContextCG.h"
#import "HTMLAttachmentElement.h"
#import "HTMLButtonElement.h"
#import "HTMLDataListElement.h"
#import "HTMLInputElement.h"
#import "HTMLMeterElement.h"
#import "HTMLNames.h"
#import "HTMLOptionElement.h"
#import "HTMLSelectElement.h"
#import "HTMLTextAreaElement.h"
#import "IOSurface.h"
#import "LocalCurrentTraitCollection.h"
#import "LocalFrame.h"
#import "LocalFrameView.h"
#import "NodeInlines.h"
#import "NodeRenderStyle.h"
#import "PaintInfo.h"
#import "PathUtilities.h"
#import "PlatformLocale.h"
#import "RenderAttachment.h"
#import "RenderBoxInlines.h"
#import "RenderButton.h"
#import "RenderMenuList.h"
#import "RenderMeter.h"
#import "RenderObject.h"
#import "RenderSlider.h"
#import "RenderStyleSetters.h"
#import "RenderView.h"
#import "Settings.h"
#import "StylePadding.h"
#import "Theme.h"
#import "TypedElementDescendantIteratorInlines.h"
#import "UTIUtilities.h"
#import "WebCoreThreadRun.h"
#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <numbers>
#import <objc/runtime.h>
#import <pal/spi/cf/CoreTextSPI.h>
#import <pal/spi/ios/UIKitSPI.h>
#import <pal/system/ios/UserInterfaceIdiom.h>
#import <wtf/MathExtras.h>
#import <wtf/NeverDestroyed.h>
#import <wtf/ObjCRuntimeExtras.h>
#import <wtf/StdLibExtras.h>
#import <wtf/cocoa/TypeCastsCocoa.h>

#import <pal/ios/UIKitSoftLink.h>

namespace WebCore {

using namespace CSS::Literals;
using namespace HTMLNames;

const float ControlBaseHeight = 20;
const float ControlBaseFontSize = 11;

RenderThemeIOS::RenderThemeIOS() = default;

RenderThemeIOS::~RenderThemeIOS() = default;

RenderTheme& RenderTheme::singleton()
{
    static NeverDestroyed<RenderThemeIOS> theme;
    return theme;
}

bool RenderThemeIOS::canCreateControlPartForRenderer(const RenderObject& renderer) const
{
    auto type = renderer.style().usedAppearance();
#if ENABLE(APPLE_PAY)
    return type == StyleAppearance::ApplePayButton;
#else
    UNUSED_PARAM(type);
    return false;
#endif
}

void RenderThemeIOS::adjustCheckboxStyle(RenderStyle& style, const Element*) const
{
    adjustMinimumIntrinsicSizeForAppearance(StyleAppearance::Checkbox, style);

    if (!style.width().isIntrinsicOrLegacyIntrinsicOrAuto() && !style.height().isAuto())
        return;

    auto size = Style::PreferredSize::Fixed { std::max(style.computedFontSize(), 10.f) };
    style.setWidth(size);
    style.setHeight(size);
}

int RenderThemeIOS::baselinePosition(const RenderBox& box) const
{
    auto baseline = RenderTheme::baselinePosition(box);
    if (!box.isHorizontalWritingMode())
        return baseline;

    if (box.style().usedAppearance() == StyleAppearance::Checkbox || box.style().usedAppearance() == StyleAppearance::Radio)
        return baseline - 2; // The baseline is 2px up from the bottom of the checkbox/radio in AppKit.
    if (box.style().usedAppearance() == StyleAppearance::Menulist)
        return baseline - 5; // This is to match AppKit. There might be a better way to calculate this though.
    return baseline;
}

bool RenderThemeIOS::isControlStyled(const RenderStyle& style) const
{
    // Buttons and MenulistButtons are styled if they contain a background image.
    if (style.usedAppearance() == StyleAppearance::PushButton || style.usedAppearance() == StyleAppearance::MenulistButton)
        return !style.visitedDependentColor(CSSPropertyBackgroundColor).isVisible() || style.backgroundLayers().hasImage();

    if (style.usedAppearance() == StyleAppearance::TextField || style.usedAppearance() == StyleAppearance::TextArea || style.usedAppearance() == StyleAppearance::SearchField)
        return style.nativeAppearanceDisabled();

    return RenderTheme::isControlStyled(style);
}

void RenderThemeIOS::adjustMinimumIntrinsicSizeForAppearance(StyleAppearance appearance, RenderStyle& style) const
{
    auto minimumControlSize = this->minimumControlSize(appearance, style.fontCascade(), { style.minWidth(), style.minHeight() }, { style.width(), style.height() }, style.usedZoom());

    // FIXME: The min-width/min-heigh value should use `calc-size()` when supported to make non-specified overrides work.

    if (auto fixedOverrideMinWidth = minimumControlSize.width().tryFixed()) {
        if (auto fixedOriginalMinWidth = style.minWidth().tryFixed()) {
            if (fixedOverrideMinWidth->value > fixedOriginalMinWidth->value)
                style.setMinWidth(Style::MinimumSize(minimumControlSize.width()));
        } else if (auto percentageOriginalMinWidth = style.minWidth().tryPercentage()) {
            // FIXME: This really makes no sense but matches existing behavior. Should use a `calc(max(override, original))` here instead.
            if (fixedOverrideMinWidth->value > percentageOriginalMinWidth->value)
                style.setMinWidth(Style::MinimumSize(minimumControlSize.width()));
        } else if (fixedOverrideMinWidth->value > 0) {
            style.setMinWidth(Style::MinimumSize(minimumControlSize.width()));
        }
    } else if (auto percentageOverrideMinWidth = minimumControlSize.width().tryPercentage()) {
        if (auto fixedOriginalMinWidth = style.minWidth().tryFixed()) {
            // FIXME: This really makes no sense but matches existing behavior. Should use a `calc(max(override, original))` here instead.
            if (percentageOverrideMinWidth->value > fixedOriginalMinWidth->value)
                style.setMinWidth(Style::MinimumSize(minimumControlSize.width()));
        } else if (auto percentageOriginalMinWidth = style.minWidth().tryPercentage()) {
            if (percentageOverrideMinWidth->value > percentageOriginalMinWidth->value)
                style.setMinWidth(Style::MinimumSize(minimumControlSize.width()));
        } else if (percentageOverrideMinWidth->value > 0) {
            style.setMinWidth(Style::MinimumSize(minimumControlSize.width()));
        }
    }
    if (auto fixedOverrideMinHeight = minimumControlSize.height().tryFixed()) {
        if (auto fixedOriginalMinHeight = style.minHeight().tryFixed()) {
            if (fixedOverrideMinHeight->value > fixedOriginalMinHeight->value)
                style.setMinHeight(Style::MinimumSize(minimumControlSize.height()));
        } else if (auto percentageOriginalMinHeight = style.minHeight().tryPercentage()) {
            // FIXME: This really makes no sense but matches existing behavior. Should use a `calc(max(override, original))` here instead.
            if (fixedOverrideMinHeight->value > percentageOriginalMinHeight->value)
                style.setMinHeight(Style::MinimumSize(minimumControlSize.height()));
        } else if (fixedOverrideMinHeight->value > 0) {
            style.setMinHeight(Style::MinimumSize(minimumControlSize.height()));
        }
    } else if (auto percentageOverrideMinHeight = minimumControlSize.height().tryPercentage()) {
        if (auto fixedOriginalMinHeight = style.minHeight().tryFixed()) {
            // FIXME: This really makes no sense but matches existing behavior. Should use a `calc(max(override, original))` here instead.
            if (percentageOverrideMinHeight->value > fixedOriginalMinHeight->value)
                style.setMinHeight(Style::MinimumSize(minimumControlSize.height()));
        } else if (auto percentageOriginalMinHeight = style.minHeight().tryPercentage()) {
            if (percentageOverrideMinHeight->value > percentageOriginalMinHeight->value)
                style.setMinHeight(Style::MinimumSize(minimumControlSize.height()));
        } else if (percentageOverrideMinHeight->value > 0) {
            style.setMinHeight(Style::MinimumSize(minimumControlSize.height()));
        }
    }
}

void RenderThemeIOS::adjustRadioStyle(RenderStyle& style, const Element*) const
{
    adjustMinimumIntrinsicSizeForAppearance(StyleAppearance::Radio, style);

    if (!style.width().isIntrinsicOrLegacyIntrinsicOrAuto() && !style.height().isAuto())
        return;

    auto size = std::max(style.computedFontSize(), 10.0f);
    style.setWidth(Style::PreferredSize::Fixed { size });
    style.setHeight(Style::PreferredSize::Fixed { size });

    auto radius = Style::LengthPercentage<CSS::Nonnegative>::Dimension { std::trunc(size / 2.0f) };
    style.setBorderRadius({ radius, radius });
}

static void applyCommonNonCapsuleBorderRadiusToStyle(RenderStyle& style)
{
    if (style.hasExplicitlySetBorderRadius())
        return;

    constexpr auto commonNonCapsuleBorderRadius = 5_css_px;
    style.setBorderRadius({ commonNonCapsuleBorderRadius, commonNonCapsuleBorderRadius });
}

void RenderThemeIOS::adjustTextFieldStyle(RenderStyle& style, const Element* element) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (element && element->document().settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::adjustTextFieldStyle(style, element);
        return;
    }
#endif

    applyCommonNonCapsuleBorderRadiusToStyle(style);

    if (!element)
        return;

    bool hasTextfieldAppearance = false;
    // Do not force a background color for elements that have a textfield
    // appearance by default, so that their background color can be styled.
    if (RefPtr input = dynamicDowncast<HTMLInputElement>(*element)) {
        // <input type=search> is the only TextFieldInputType that has a
        // non-textfield appearance value.
        hasTextfieldAppearance = input->isTextField() && !input->isSearchField();
    }

    auto adjustBackgroundColor = [&] {
        auto styleColorOptions = element->document().styleColorOptions(&style);
        if (style.backgroundColor() != systemColor(CSSValueAppleSystemOpaqueTertiaryFill, styleColorOptions))
            return;

        style.setBackgroundColor(systemColor(CSSValueWebkitControlBackground, styleColorOptions));
    };

    if (PAL::currentUserInterfaceIdiomIsVision()) {
        if (hasTextfieldAppearance)
            style.setBackgroundColor(Color::transparentBlack);
        else
            adjustBackgroundColor();
        style.resetBorderExceptRadius();
        return;
    }

    if (hasTextfieldAppearance)
        return;

    adjustBackgroundColor();
}

void RenderThemeIOS::paintTextFieldInnerShadow(const PaintInfo& paintInfo, const FloatRoundedRect& roundedRect)
{
    auto& context = paintInfo.context();

    const FloatSize innerShadowOffset { 0, 5 };
    constexpr auto innerShadowBlur = 10.0f;
    auto innerShadowColor = DisplayP3<float> { 0, 0, 0, 0.04f };
    context.setDropShadow({ innerShadowOffset, innerShadowBlur, innerShadowColor, ShadowRadiusMode::Default });
    context.setFillColor(Color::black);

    Path innerShadowPath;
    FloatRect innerShadowRect = roundedRect.rect();
    innerShadowRect.inflate(std::max<float>(innerShadowOffset.width(), innerShadowOffset.height()) + innerShadowBlur);
    innerShadowPath.addRect(innerShadowRect);

    FloatRoundedRect innerShadowHoleRect = roundedRect;
    // FIXME: This is not from the spec; but without it we get antialiasing fringe from the fill; we need a better solution.
    innerShadowHoleRect.inflate(0.5);
    innerShadowPath.addRoundedRect(innerShadowHoleRect);

    context.setFillRule(WindRule::EvenOdd);
    context.fillPath(innerShadowPath);
}

void RenderThemeIOS::paintTextFieldDecorations(const RenderBox& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    auto shouldPaintFillAndInnerShadow = false;
    auto element = box.element();
    if (RefPtr input = dynamicDowncast<HTMLInputElement>(*element)) {
        if (input->isTextField() && !input->isSearchField())
            shouldPaintFillAndInnerShadow = true;
    } else if (is<HTMLTextAreaElement>(*element))
        shouldPaintFillAndInnerShadow = true;

    if (PAL::currentUserInterfaceIdiomIsVision() && shouldPaintFillAndInnerShadow) {
        auto borderShape = BorderShape::shapeForBorderRect(box.style(), LayoutRect(rect));
        auto path = borderShape.pathForOuterShape(box.document().deviceScaleFactor());
        context.setFillColor(Color::black.colorWithAlphaByte(10));
        context.drawPath(path);
        context.clipPath(path);
        paintTextFieldInnerShadow(paintInfo,  borderShape.deprecatedPixelSnappedRoundedRect(box.document().deviceScaleFactor()));
    }
}

void RenderThemeIOS::adjustTextAreaStyle(RenderStyle& style, const Element* element) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (element && element->document().settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::adjustTextAreaStyle(style, element);
        return;
    }
#else
    UNUSED_PARAM(element);
#endif

    applyCommonNonCapsuleBorderRadiusToStyle(style);

    if (!PAL::currentUserInterfaceIdiomIsVision())
        return;

    style.setBackgroundColor(Color::transparentBlack);
    style.resetBorderExceptRadius();
}

void RenderThemeIOS::paintTextAreaDecorations(const RenderBox& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
    paintTextFieldDecorations(box, paintInfo, rect);
}

// These values are taken from the UIKit button system.
constexpr auto largeButtonSize = 45;
constexpr auto largeButtonBorderRadiusRatio = 0.35f / 2;

const int MenuListMinHeight = 15;

const float MenuListBaseHeight = 20;
const float MenuListBaseFontSize = 11;

static Style::PaddingEdge toTruncatedPaddingEdge(auto value)
{
    return Style::PaddingEdge::Fixed { static_cast<float>(std::trunc(value)) };
}

Style::PaddingBox RenderThemeIOS::popupInternalPaddingBox(const RenderStyle& style) const
{
    auto emSize = CSSPrimitiveValue::create(1.0, CSSUnitType::CSS_EM);
    auto padding = emSize->resolveAsLength<float>({ style, nullptr, nullptr, nullptr });

    if (style.usedAppearance() == StyleAppearance::MenulistButton) {
        auto value = toTruncatedPaddingEdge(padding + Style::evaluate(style.borderTopWidth()));
        if (style.writingMode().isBidiRTL())
            return { 0_css_px, 0_css_px, 0_css_px, value };
        return { 0_css_px, value, 0_css_px, 0_css_px };
    }
    return { 0_css_px };
}

static inline bool canAdjustBorderRadiusForAppearance(StyleAppearance appearance, const RenderBox& box)
{
    switch (appearance) {
#if ENABLE(APPLE_PAY)
    case StyleAppearance::ApplePayButton:
#endif
    case StyleAppearance::None:
    case StyleAppearance::SearchField:
        return false;
    case StyleAppearance::MenulistButton:
        return !box.style().hasExplicitlySetBorderRadius();
    default:
        return true;
    };
}

void RenderThemeIOS::adjustRoundBorderRadius(RenderStyle& style, RenderBox& box)
{
    if (!canAdjustBorderRadiusForAppearance(style.usedAppearance(), box) || style.backgroundLayers().hasImage())
        return;

    auto boxLogicalHeight = box.logicalHeight();
    auto minDimension = std::min(box.width(), box.height());

    if ((is<RenderButton>(box) || is<RenderMenuList>(box)) && boxLogicalHeight >= largeButtonSize) {
        auto largeButtonBorderRadius = Style::LengthPercentage<CSS::Nonnegative>::Dimension { minDimension * largeButtonBorderRadiusRatio };
        style.setBorderRadius({ largeButtonBorderRadius, largeButtonBorderRadius });
        return;
    }

    // FIXME: We should not be relying on border radius for the appearance of our controls <rdar://problem/7675493>.
    auto borderRadius = Style::BorderRadiusValue {
        Style::LengthPercentage<CSS::Nonnegative>::Dimension { minDimension / 2 },
        Style::LengthPercentage<CSS::Nonnegative>::Dimension { boxLogicalHeight / 2 },
    };
    if (!style.writingMode().isHorizontal())
        borderRadius = { borderRadius.height(), borderRadius.width() };

    style.setBorderRadius(WTFMove(borderRadius));
}

static void applyCommonButtonPaddingToStyle(RenderStyle& style, const Element& element)
{
    Ref document = element.document();
    Ref emSize = CSSPrimitiveValue::create(0.5, CSSUnitType::CSS_EM);
    // We don't need this element's parent style to calculate `em` units, so it's okay to pass nullptr for it here.
    auto edge = toTruncatedPaddingEdge(emSize->resolveAsLength<int>({ style, document->renderStyle(), nullptr, document->renderView() }));

    auto paddingBox = Style::PaddingBox { 0_css_px, edge, 0_css_px, edge };
    if (!style.writingMode().isHorizontal())
        paddingBox = { paddingBox.left(), paddingBox.top(), paddingBox.right(), paddingBox.bottom() };

    style.setPaddingBox(WTFMove(paddingBox));
}

static void adjustSelectListButtonStyle(RenderStyle& style, const Element& element)
{
    // Enforce "padding: 0 0.5em".
    applyCommonButtonPaddingToStyle(style, element);

    // Enforce "line-height: normal".
    style.setLineHeight(Length(LengthType::Normal));
}

class RenderThemeMeasureTextClient : public MeasureTextClient {
public:
    RenderThemeMeasureTextClient(const FontCascade& font, const RenderStyle& style)
        : m_font(font)
        , m_style(style)
    {
    }
    float measureText(const String& string) const override
    {
        TextRun run = RenderBlock::constructTextRun(string, m_style);
        return m_font.width(run);
    }
private:
    const FontCascade& m_font;
    const RenderStyle& m_style;
};

static void adjustInputElementButtonStyle(RenderStyle& style, const HTMLInputElement& inputElement)
{
    // Always Enforce "padding: 0 0.5em".
    applyCommonButtonPaddingToStyle(style, inputElement);

    applyCommonNonCapsuleBorderRadiusToStyle(style);

    // Don't adjust the style if the width is specified.
    if (auto fixedLogicalWidth = style.logicalWidth().tryFixed(); fixedLogicalWidth && fixedLogicalWidth->value > 0)
        return;

    // Don't adjust for unsupported date input types.
    DateComponentsType dateType = inputElement.dateType();
    if (dateType == DateComponentsType::Invalid)
        return;

#if !ENABLE(INPUT_TYPE_WEEK_PICKER)
    if (dateType == DateComponentsType::Week)
        return;
#endif

    // Enforce the width and set the box-sizing to content-box to not conflict with the padding.
    FontCascade font = style.fontCascade();

    float maximumWidth = localizedDateCache().estimatedMaximumWidthForDateType(dateType, font, RenderThemeMeasureTextClient(font, style));

    ASSERT(maximumWidth >= 0);

    if (maximumWidth > 0) {
        style.setLogicalMinWidth(Style::MinimumSize::Fixed { std::ceil(maximumWidth) });
        style.setBoxSizing(BoxSizing::ContentBox);
    }
}

void RenderThemeIOS::adjustMenuListButtonStyle(RenderStyle& style, const Element* element) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (element && element->document().settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::adjustMenuListButtonStyle(style, element);
        return;
    }
#endif

    // Set the min-height to be at least MenuListMinHeight.
    if (style.logicalHeight().isAuto())
        style.setLogicalMinHeight(Style::MinimumSize::Fixed { static_cast<float>(std::max(MenuListMinHeight, static_cast<int>(MenuListBaseHeight / MenuListBaseFontSize * style.fontDescription().computedSize()))) });
    else
        style.setLogicalMinHeight(Style::MinimumSize::Fixed { static_cast<float>(MenuListMinHeight) });

    if (!element)
        return;

    adjustButtonLikeControlStyle(style, *element);

    // Enforce some default styles in the case that this is a non-multiple <select> element,
    // or a date input. We don't force these if this is just an element with
    // "-webkit-appearance: menulist-button".
    if (is<HTMLSelectElement>(*element) && !element->hasAttributeWithoutSynchronization(HTMLNames::multipleAttr))
        adjustSelectListButtonStyle(style, *element);
    else if (RefPtr input = dynamicDowncast<HTMLInputElement>(*element))
        adjustInputElementButtonStyle(style, *input);
}

void RenderThemeIOS::paintMenuListButtonDecorations(const RenderBox& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (box.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::paintMenuListButtonDecorations(box, paintInfo, rect);
#endif

    if (is<HTMLInputElement>(box.element()))
        return;

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    auto& style = box.style();

    Path glyphPath;
    FloatSize glyphSize;

    if (box.isRenderMenuList() && downcast<HTMLSelectElement>(box.element())->multiple()) {
        constexpr int length = 18;
        constexpr int count = 3;
        constexpr int padding = 12;

        FloatRect ellipse(0, 0, length, length);

        for (int i = 0; i < count; ++i) {
            glyphPath.addEllipseInRect(ellipse);
            ellipse.move(length + padding, 0);
        }

        glyphSize = { length * count + padding * (count - 1), length };
    } else {
        constexpr int glyphWidth = 63;
        constexpr int glyphHeight = 73;
        glyphSize = { glyphWidth, glyphHeight };

        glyphPath.moveTo({ 31.8593f, 1.0f });
        glyphPath.addBezierCurveTo({ 30.541f, 1.0f }, { 29.418f, 1.586f }, { 28.0507f, 2.66f });
        glyphPath.addLineTo({ 2.5625f, 23.168f });
        glyphPath.addBezierCurveTo({ 1.5859f, 23.998f }, { 1.0f, 25.2188f }, { 1.0f, 26.7325f });
        glyphPath.addBezierCurveTo({ 1.0f, 29.6133f }, { 3.246f, 31.7129f }, { 5.9316f, 31.7129f });
        glyphPath.addBezierCurveTo({ 7.1523f, 31.7129f }, { 8.3242f, 31.2246f }, { 9.5449f, 30.248f });
        glyphPath.addLineTo({ 31.8593f, 12.377f });
        glyphPath.addLineTo({ 54.2226f, 30.248f });
        glyphPath.addBezierCurveTo({ 55.3945f, 31.2246f }, { 56.6152f, 31.7129f }, { 57.7871f, 31.7129 });
        glyphPath.addBezierCurveTo({ 60.4726f, 31.7129f }, { 62.7187f, 29.6133f }, { 62.7187f, 26.7325 });
        glyphPath.addBezierCurveTo({ 62.7187f, 25.2188f }, { 62.1327f, 23.9981f }, { 61.1562f, 23.168 });
        glyphPath.addLineTo({ 35.6679f, 2.6602f });
        glyphPath.addBezierCurveTo({ 34.3496f, 1.586f }, { 33.1777f, 1.0f }, { 31.8593f, 1.0f });
        glyphPath.moveTo({ 31.8593f, 72.3867f });
        glyphPath.addBezierCurveTo({ 33.1777f, 72.3867f }, { 34.3496f, 71.8007f }, { 35.6679f, 70.7266f });
        glyphPath.addLineTo({ 61.1562f, 50.2188f });
        glyphPath.addBezierCurveTo({ 62.1328f, 49.3888f }, { 62.7187f, 48.168f }, { 62.7187f, 46.6543f });
        glyphPath.addBezierCurveTo({ 62.7187f, 43.7735f }, { 60.4726f, 41.6739f }, { 57.7871f, 41.6739f });
        glyphPath.addBezierCurveTo({ 56.6151f, 41.6739f }, { 55.3945f, 42.162f }, { 54.2226f, 43.09f });
        glyphPath.addLineTo({ 31.8593f, 61.01f });
        glyphPath.addLineTo({ 9.545f, 43.0898f });
        glyphPath.addBezierCurveTo({ 8.3243f, 42.1619f }, { 7.1524f, 41.6738f }, { 5.9317f, 41.6738f });
        glyphPath.addBezierCurveTo({ 3.246f, 41.6739f }, { 1.0f, 43.7735f }, { 1.0f, 46.6543f });
        glyphPath.addBezierCurveTo({ 1.0f, 48.168f }, { 1.5859, 49.3887 }, { 2.5625, 50.2188f });
        glyphPath.addLineTo({ 28.0507f, 70.7266f });
        glyphPath.addBezierCurveTo({ 29.4179f, 71.8f }, { 30.541f, 72.3867f }, { 31.8593f, 72.3867 });
    }

    auto emSize = CSSPrimitiveValue::create(1.0, CSSUnitType::CSS_EM);
    auto emPixels = emSize->resolveAsLength<float>({ style, nullptr, nullptr, nullptr });
    auto glyphScale = 0.65f * emPixels / glyphSize.width();
    glyphSize = glyphScale * glyphSize;

    bool isHorizontalWritingMode = style.writingMode().isHorizontal();
    auto logicalRect = isHorizontalWritingMode ? rect : rect.transposedRect();

    FloatPoint glyphOrigin;
    glyphOrigin.setY(logicalRect.center().y() - glyphSize.height() / 2.0f);
    if (!style.writingMode().isInlineFlipped())
        glyphOrigin.setX(logicalRect.maxX() - glyphSize.width() - Style::evaluate(box.style().borderEndWidth()) - Style::evaluate(box.style().paddingEnd(), logicalRect.width()));
    else
        glyphOrigin.setX(logicalRect.x() + Style::evaluate(box.style().borderEndWidth()) + Style::evaluate(box.style().paddingEnd(), logicalRect.width()));

    if (!isHorizontalWritingMode)
        glyphOrigin = glyphOrigin.transposedPoint();

    AffineTransform transform;
    transform.translate(glyphOrigin);
    transform.scale(glyphScale);
    glyphPath.transform(transform);

    if (isEnabled(box))
        context.setFillColor(style.color());
    else
        context.setFillColor(systemColor(CSSValueAppleSystemTertiaryLabel, box.styleColorOptions()));

    context.fillPath(glyphPath);
}

constexpr auto defaultTrackThickness = 4.0;
constexpr auto defaultTrackRadius = defaultTrackThickness / 2.0;
constexpr auto defaultSliderThumbSize = 16_css_px;

void RenderThemeIOS::adjustSliderTrackStyle(RenderStyle& style, const Element* element) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (element && element->document().settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::adjustSliderTrackStyle(style, element);
        return;
    }
#endif

    RenderTheme::adjustSliderTrackStyle(style, element);

    // FIXME: We should not be relying on border radius for the appearance of our controls <rdar://problem/7675493>.
    constexpr auto radius = Style::LengthPercentage<CSS::Nonnegative>::Dimension { defaultTrackRadius };
    style.setBorderRadius({ radius, radius });
}

constexpr auto nativeControlBorderInlineSize = 1.0f;

bool RenderThemeIOS::paintSliderTrack(const RenderObject& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (box.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::paintSliderTrack(box, paintInfo, rect);
#endif

    auto* renderSlider = dynamicDowncast<RenderSlider>(box);
    if (!renderSlider)
        return true;

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    bool isHorizontal = true;
    FloatRect trackClip = rect;

    switch (box.style().usedAppearance()) {
    case StyleAppearance::SliderHorizontal:
        // Inset slightly so the thumb covers the edge.
        if (trackClip.width() > 2) {
            trackClip.setWidth(trackClip.width() - 2);
            trackClip.setX(trackClip.x() + 1);
        }
        trackClip.setHeight(defaultTrackThickness);
        trackClip.setY(rect.y() + rect.height() / 2 - defaultTrackThickness / 2);
        break;
    case StyleAppearance::SliderVertical:
        isHorizontal = false;
        // Inset slightly so the thumb covers the edge.
        if (trackClip.height() > 2) {
            trackClip.setHeight(trackClip.height() - 2);
            trackClip.setY(trackClip.y() + 1);
        }
        trackClip.setWidth(defaultTrackThickness);
        trackClip.setX(rect.x() + rect.width() / 2 - defaultTrackThickness / 2);
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    auto styleColorOptions = box.styleColorOptions();

    auto cornerWidth = trackClip.width() < defaultTrackThickness ? trackClip.width() / 2.0f : defaultTrackRadius;
    auto cornerHeight = trackClip.height() < defaultTrackThickness ? trackClip.height() / 2.0f : defaultTrackRadius;

    FloatRoundedRect::Radii cornerRadii(cornerWidth, cornerHeight);
    FloatRoundedRect innerBorder(trackClip, cornerRadii);

    FloatRoundedRect outerBorder(innerBorder);
    outerBorder.inflateWithRadii(nativeControlBorderInlineSize);
    context.fillRoundedRect(outerBorder, systemColor(CSSValueWebkitControlBackground, styleColorOptions));

    context.fillRoundedRect(innerBorder, systemColor(CSSValueAppleSystemOpaqueFill, styleColorOptions));

    paintSliderTicks(box, paintInfo, trackClip);

    double valueRatio = renderSlider->valueRatio();
    if (isHorizontal) {
        double newWidth = trackClip.width() * valueRatio;

        if (box.writingMode().isInlineFlipped())
            trackClip.move(trackClip.width() - newWidth, 0);

        trackClip.setWidth(newWidth);
    } else {
        float height = trackClip.height();
        trackClip.setHeight(height * valueRatio);

        if (box.writingMode().isHorizontal() || box.writingMode().isInlineFlipped())
            trackClip.setY(trackClip.y() + height - trackClip.height());
    }

    FloatRoundedRect fillRect(trackClip, cornerRadii);
    context.fillRoundedRect(fillRect, controlTintColor(box.style(), styleColorOptions));

    return false;
}

void RenderThemeIOS::adjustSliderThumbSize(RenderStyle& style, const Element* element) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (element && element->document().settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::adjustSliderThumbSize(style, element);
        return;
    }
#else
    UNUSED_PARAM(element);
#endif

    if (style.usedAppearance() != StyleAppearance::SliderThumbHorizontal && style.usedAppearance() != StyleAppearance::SliderThumbVertical)
        return;

    // Enforce "border-radius: 50%".
    style.setBorderRadius({ 50_css_percentage, 50_css_percentage });

    // Enforce a 16x16 size if no size is provided.
    if (style.width().isIntrinsicOrLegacyIntrinsicOrAuto() || style.height().isAuto()) {
        style.setWidth(defaultSliderThumbSize);
        style.setHeight(defaultSliderThumbSize);
    }
}

constexpr auto reducedMotionProgressAnimationMinOpacity = 0.3f;
constexpr auto reducedMotionProgressAnimationMaxOpacity = 0.6f;

bool RenderThemeIOS::paintProgressBar(const RenderObject& renderer, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (renderer.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::paintProgressBar(renderer, paintInfo, rect);
#endif

    auto* renderProgress = dynamicDowncast<RenderProgress>(renderer);
    if (!renderProgress)
        return true;

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    auto styleColorOptions = renderer.styleColorOptions();
    auto isHorizontalWritingMode = renderer.writingMode().isHorizontal();

    constexpr auto barBlockSize = 4.0f;

    constexpr auto barCornerRadiusInlineSize = 2.5f;
    constexpr auto barCornerRadiusBlockSize = 1.5f;

    FloatRoundedRect::Radii barCornerRadii(
        isHorizontalWritingMode ? barCornerRadiusInlineSize : barCornerRadiusBlockSize,
        isHorizontalWritingMode ? barCornerRadiusBlockSize : barCornerRadiusInlineSize
    );

    auto logicalRect = isHorizontalWritingMode ? rect : rect.transposedRect();

    float rectInlineSize = logicalRect.width();
    float rectInlineStart = logicalRect.x();
    float rectBlockSize = logicalRect.height();
    float rectBlockStart = logicalRect.y();

    if (rectBlockSize < barBlockSize) {
        // The rect is smaller than the standard progress bar. We clip to the
        // element's rect to avoid leaking pixels outside the repaint rect.
        context.clip(rect);
    }

    float trackInlineStart = rectInlineStart + nativeControlBorderInlineSize;
    float trackBlockStart = rectBlockStart + (rectBlockSize - barBlockSize) / 2.0f;
    float trackInlineSize = rectInlineSize - 2 * nativeControlBorderInlineSize;

    FloatRect trackRect(trackInlineStart, trackBlockStart, trackInlineSize, barBlockSize);
    FloatRoundedRect roundedTrackRect(isHorizontalWritingMode ? trackRect : trackRect.transposedRect(), barCornerRadii);

    FloatRoundedRect roundedTrackBorderRect(roundedTrackRect);
    roundedTrackBorderRect.inflateWithRadii(nativeControlBorderInlineSize);
    context.fillRoundedRect(roundedTrackBorderRect, systemColor(CSSValueWebkitControlBackground, styleColorOptions));

    context.fillRoundedRect(roundedTrackRect, systemColor(CSSValueAppleSystemOpaqueFill, styleColorOptions));

    float barInlineSize;
    float barInlineStart = trackInlineStart;
    float barBlockStart = trackBlockStart;
    float alpha = 1.0f;

    if (renderProgress->isDeterminate()) {
        barInlineSize = clampTo<float>(renderProgress->position(), 0.0f, 1.0f) * trackInlineSize;

        if (renderProgress->writingMode().isInlineFlipped())
            barInlineStart = trackInlineStart + trackInlineSize - barInlineSize;
    } else {
        Seconds elapsed = MonotonicTime::now() - renderProgress->animationStartTime();
        float position = fmodf(elapsed.value(), 1.0f);
        bool reverseDirection = static_cast<int>(elapsed.value()) % 2;

        if (Theme::singleton().userPrefersReducedMotion()) {
            barInlineSize = trackInlineSize;

            float difference = position * (reducedMotionProgressAnimationMaxOpacity - reducedMotionProgressAnimationMinOpacity);
            if (reverseDirection)
                alpha = reducedMotionProgressAnimationMaxOpacity - difference;
            else
                alpha = reducedMotionProgressAnimationMinOpacity + difference;
        } else {
            barInlineSize = 0.25f * trackInlineSize;

            float offset = position * (trackInlineSize + barInlineSize);
            if (reverseDirection)
                barInlineStart = trackInlineStart + trackInlineSize - offset;
            else
                barInlineStart -= barInlineSize - offset;

            context.clipRoundedRect(roundedTrackRect);
        }
    }

    FloatRect barRect(barInlineStart, barBlockStart, barInlineSize, barBlockSize);
    context.fillRoundedRect(FloatRoundedRect(isHorizontalWritingMode ? barRect : barRect.transposedRect(), barCornerRadii), controlTintColor(renderer.style(), styleColorOptions).colorWithAlphaMultipliedBy(alpha));

    return false;
}

IntSize RenderThemeIOS::sliderTickSize() const
{
    // FIXME: <rdar://problem/12271791> MERGEBOT: Correct values for slider tick of <input type="range"> elements (requires ENABLE_DATALIST_ELEMENT)
    return IntSize(1, 3);
}

int RenderThemeIOS::sliderTickOffsetFromTrackCenter() const
{
    // FIXME: <rdar://problem/12271791> MERGEBOT: Correct values for slider tick of <input type="range"> elements (requires ENABLE_DATALIST_ELEMENT)
    return -9;
}

void RenderThemeIOS::adjustSearchFieldStyle(RenderStyle& style, const Element* element) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (element && element->document().settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::adjustSearchFieldStyle(style, element);
        return;
    }
#endif

    RenderTheme::adjustSearchFieldStyle(style, element);

    applyCommonNonCapsuleBorderRadiusToStyle(style);

    if (!element)
        return;

    if (!style.hasBorder())
        return;

    RenderBox* box = element->renderBox();
    if (!box)
        return;

    adjustRoundBorderRadius(style, *box);
}

void RenderThemeIOS::paintSearchFieldDecorations(const RenderBox& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
    paintTextFieldDecorations(box, paintInfo, rect);
}

// This value matches the opacity applied to UIKit controls.
constexpr auto pressedStateOpacity = 0.75f;

bool RenderThemeIOS::isSubmitStyleButton(const Element& element) const
{
    if (RefPtr input = dynamicDowncast<HTMLInputElement>(element))
        return input->isSubmitButton();

    if (RefPtr button = dynamicDowncast<HTMLButtonElement>(element))
        return button->isExplicitlySetSubmitButton();

    return false;
}

void RenderThemeIOS::adjustButtonLikeControlStyle(RenderStyle& style, const Element& element) const
{
    if (PAL::currentUserInterfaceIdiomIsVision())
        return;

    if (element.isDisabledFormControl())
        return;

    if (!style.hasAutoAccentColor()) {
        auto tintColor = style.usedAccentColor(element.document().styleColorOptions(&style));
        if (isSubmitStyleButton(element))
            style.setBackgroundColor(WTFMove(tintColor));
        else
            style.setColor(WTFMove(tintColor));
    }

    if (!element.active())
        return;

    auto textColor = style.color();
    if (textColor.isValid())
        style.setColor(textColor.colorWithAlphaMultipliedBy(pressedStateOpacity));

    auto backgroundColor = style.colorResolvingCurrentColor(style.backgroundColor());
    if (backgroundColor.isValid())
        style.setBackgroundColor(backgroundColor.colorWithAlphaMultipliedBy(pressedStateOpacity));
}

void RenderThemeIOS::adjustButtonStyle(RenderStyle& style, const Element* element) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (element && element->document().settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::adjustButtonStyle(style, element);
        return;
    }
#endif

    // If no size is specified, ensure the height of the button matches ControlBaseHeight scaled
    // with the font size. min-height is used rather than height to avoid clipping the contents of
    // the button in cases where the button contains more than one line of text.
    if (style.logicalWidth().isIntrinsicOrLegacyIntrinsicOrAuto() || style.logicalHeight().isAuto()) {
        auto minimumHeight = ControlBaseHeight / ControlBaseFontSize * style.fontDescription().computedSize();
        if (auto fixedLogicalMinHeight = style.logicalMinHeight().tryFixed())
            minimumHeight = std::max(minimumHeight, fixedLogicalMinHeight->value);
        // FIXME: This may need to be a layout time adjustment to support various values like fit-content etc.
        style.setLogicalMinHeight(Style::MinimumSize::Fixed { minimumHeight });
    }

    if (style.usedAppearance() == StyleAppearance::ColorWell)
        return;

    // Set padding: 0 1.0em; on buttons.
    // CSSPrimitiveValue::resolveAsLength only needs the element's style to calculate em lengths.
    // Since the element might not be in a document, just pass nullptr for the root element style,
    // the parent element style, and the render view.
    auto emSize = CSSPrimitiveValue::create(1.0, CSSUnitType::CSS_EM);
    auto edge = toTruncatedPaddingEdge(emSize->resolveAsLength<int>({ style, nullptr, nullptr, nullptr }));

    auto paddingBox = Style::PaddingBox { 0_css_px, edge, 0_css_px, edge };
    if (!style.writingMode().isHorizontal())
        paddingBox = { paddingBox.left(), paddingBox.top(), paddingBox.right(), paddingBox.bottom() };

    style.setPaddingBox(WTFMove(paddingBox));

    if (!element)
        return;

    adjustButtonLikeControlStyle(style, *element);

    RenderBox* box = element->renderBox();
    if (!box)
        return;

    adjustRoundBorderRadius(style, *box);
}

Color RenderThemeIOS::platformActiveSelectionBackgroundColor(OptionSet<StyleColorOptions>) const
{
    return Color::transparentBlack;
}

Color RenderThemeIOS::platformInactiveSelectionBackgroundColor(OptionSet<StyleColorOptions>) const
{
    return Color::transparentBlack;
}

static std::optional<Color>& cachedFocusRingColor()
{
    static NeverDestroyed<std::optional<Color>> color;
    return color;
}

static std::optional<Color>& cachedInsertionPointColor()
{
    static NeverDestroyed<std::optional<Color>> color;
    return color;
}

Color RenderThemeIOS::systemFocusRingColor()
{
    if (!cachedFocusRingColor().has_value()) {
        // FIXME: Should be using +keyboardFocusIndicatorColor. For now, work around <rdar://problem/50838886>.
        cachedFocusRingColor() = colorFromCocoaColor([PAL::getUIColorClass() systemBlueColor]);
    }
    return *cachedFocusRingColor();
}

Color RenderThemeIOS::platformFocusRingColor(OptionSet<StyleColorOptions>) const
{
    return systemFocusRingColor();
}

Color RenderThemeIOS::insertionPointColor()
{
    if (!cachedInsertionPointColor().has_value())
        cachedInsertionPointColor() = Color::transparentBlack;
    return *cachedInsertionPointColor();
}

Color RenderThemeIOS::autocorrectionReplacementMarkerColor(const RenderText& renderer) const
{
    auto caretColor = CaretBase::computeCaretColor(renderer.style(), renderer.textNode());
    if (!caretColor.isValid())
        caretColor = insertionPointColor();

    auto hsla = caretColor.toColorTypeLossy<HSLA<float>>().resolved();
    if (hsla.hue) {
        hsla.saturation = 100;
        if (renderer.styleColorOptions().contains(StyleColorOptions::UseDarkAppearance)) {
            hsla.lightness = 50;
            hsla.alpha = 0.5f;
        } else {
            hsla.lightness = 41;
            hsla.alpha = 0.3f;
        }

        return hsla;
    }

    return caretColor.colorWithAlpha(0.3);
}

Color RenderThemeIOS::platformAnnotationHighlightColor(OptionSet<StyleColorOptions>) const
{
    // FIXME: expose the real value from UIKit.
    return SRGBA<uint8_t> { 255, 238, 190 };
}

bool RenderThemeIOS::shouldHaveSpinButton(const HTMLInputElement&) const
{
    return false;
}

bool RenderThemeIOS::supportsFocusRing(const RenderObject& renderer, const RenderStyle& style) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (renderer.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::supportsFocusRing(renderer, style);
#else
    UNUSED_PARAM(renderer);
    UNUSED_PARAM(style);
#endif
    return false;
}

bool RenderThemeIOS::supportsBoxShadow(const RenderStyle& style) const
{
    // FIXME: See if additional native controls can support box shadows.
    switch (style.usedAppearance()) {
    case StyleAppearance::SliderThumbHorizontal:
    case StyleAppearance::SliderThumbVertical:
        return true;
    default:
        return false;
    }
}

struct CSSValueSystemColorInformation {
    CSSValueID cssValueID;
    SEL selector;
    bool makeOpaque { false };
    float opacity { 1.0f };
};

static const Vector<CSSValueSystemColorInformation>& cssValueSystemColorInformationList()
{
    static NeverDestroyed<Vector<CSSValueSystemColorInformation>> cssValueSystemColorInformationList;

    static std::once_flag initializeOnce;
    std::call_once(
        initializeOnce,
        [] {
        cssValueSystemColorInformationList.get() = Vector(std::initializer_list<CSSValueSystemColorInformation> {
            { CSSValueCanvas, @selector(systemBackgroundColor) },
            { CSSValueCanvastext, @selector(labelColor) },
            { CSSValueText, @selector(labelColor) },
            { CSSValueWebkitControlBackground, @selector(systemBackgroundColor) },
            { CSSValueAppleSystemBlue, @selector(systemBlueColor) },
            { CSSValueAppleSystemBrown, @selector(systemBrownColor) },
            { CSSValueAppleSystemGray, @selector(systemGrayColor) },
            { CSSValueAppleSystemGreen, @selector(systemGreenColor) },
            { CSSValueAppleSystemIndigo, @selector(systemIndigoColor) },
            { CSSValueAppleSystemOrange, @selector(systemOrangeColor) },
            { CSSValueAppleSystemPink, @selector(systemPinkColor) },
            { CSSValueAppleSystemPurple, @selector(systemPurpleColor) },
            { CSSValueAppleSystemRed, @selector(systemRedColor) },
            { CSSValueAppleSystemTeal, @selector(systemTealColor) },
            { CSSValueAppleSystemYellow, @selector(systemYellowColor) },
            { CSSValueAppleSystemBackground, @selector(systemBackgroundColor) },
            { CSSValueAppleSystemSecondaryBackground, @selector(secondarySystemBackgroundColor) },
            { CSSValueAppleSystemTertiaryBackground, @selector(tertiarySystemBackgroundColor) },
            { CSSValueAppleSystemOpaqueFill, @selector(systemFillColor), true },
            { CSSValueAppleSystemOpaqueSecondaryFill, @selector(secondarySystemFillColor), true },
            // FIXME: <rdar://problem/75538507> UIKit should expose this color so that we maintain parity with system buttons.
            { CSSValueAppleSystemOpaqueSecondaryFillDisabled, @selector(secondarySystemFillColor), true, 0.75f },
            { CSSValueAppleSystemOpaqueTertiaryFill, @selector(tertiarySystemFillColor), true },
            { CSSValueAppleSystemTertiaryFill, @selector(tertiarySystemFillColor) },
            { CSSValueAppleSystemQuaternaryFill, @selector(quaternarySystemFillColor) },
            { CSSValueAppleSystemGroupedBackground, @selector(systemGroupedBackgroundColor) },
            { CSSValueAppleSystemSecondaryGroupedBackground, @selector(secondarySystemGroupedBackgroundColor) },
            { CSSValueAppleSystemTertiaryGroupedBackground, @selector(tertiarySystemGroupedBackgroundColor) },
            { CSSValueAppleSystemLabel, @selector(labelColor) },
            { CSSValueAppleSystemSecondaryLabel, @selector(secondaryLabelColor) },
            { CSSValueAppleSystemTertiaryLabel, @selector(tertiaryLabelColor) },
            { CSSValueAppleSystemQuaternaryLabel, @selector(quaternaryLabelColor) },
            { CSSValueAppleSystemPlaceholderText, @selector(placeholderTextColor) },
            { CSSValueAppleSystemSeparator, @selector(separatorColor) },
            // FIXME: <rdar://problem/79471528> Adopt [UIColor opaqueSeparatorColor] once it has a high contrast variant.
            { CSSValueAppleSystemOpaqueSeparator, @selector(separatorColor), true },
            { CSSValueAppleSystemContainerBorder, @selector(separatorColor) },
            { CSSValueAppleSystemControlBackground, @selector(systemBackgroundColor) },
            { CSSValueAppleSystemGrid, @selector(separatorColor) },
            { CSSValueAppleSystemHeaderText, @selector(labelColor) },
            { CSSValueAppleSystemSelectedContentBackground, @selector(tableCellDefaultSelectionTintColor) },
            { CSSValueAppleSystemTextBackground, @selector(systemBackgroundColor) },
            { CSSValueAppleSystemUnemphasizedSelectedContentBackground, @selector(tableCellDefaultSelectionTintColor) },
            { CSSValueAppleWirelessPlaybackTargetActive, @selector(systemBlueColor) },
        });
    });

    return cssValueSystemColorInformationList;
}

static inline std::optional<Color> systemColorFromCSSValueSystemColorInformation(CSSValueSystemColorInformation systemColorInformation, bool useDarkAppearance)
{
    UIColor *color = wtfObjCMsgSend<UIColor *>(PAL::getUIColorClass(), systemColorInformation.selector);
    if (!color)
        return std::nullopt;

    Color systemColor(roundAndClampToSRGBALossy(color.CGColor), Color::Flags::Semantic);

    if (systemColorInformation.opacity < 1.0f)
        systemColor = systemColor.colorWithAlphaMultipliedBy(systemColorInformation.opacity);

    if (systemColorInformation.makeOpaque)
        return blendSourceOver(useDarkAppearance ? Color::black : Color::white, systemColor);

    return systemColor;
}

static std::optional<Color> systemColorFromCSSValueID(CSSValueID cssValueID, bool useDarkAppearance, bool useElevatedUserInterfaceLevel)
{
    LocalCurrentTraitCollection localTraitCollection(useDarkAppearance, useElevatedUserInterfaceLevel);

    for (auto& cssValueSystemColorInformation : cssValueSystemColorInformationList()) {
        if (cssValueSystemColorInformation.cssValueID == cssValueID)
            return systemColorFromCSSValueSystemColorInformation(cssValueSystemColorInformation, useDarkAppearance);
    }

    return std::nullopt;
}

static RenderThemeIOS::CSSValueToSystemColorMap& globalCSSValueToSystemColorMap()
{
    static NeverDestroyed<RenderThemeIOS::CSSValueToSystemColorMap> colorMap;
    return colorMap;
}

const RenderThemeIOS::CSSValueToSystemColorMap& RenderThemeIOS::cssValueToSystemColorMap()
{
    ASSERT(RunLoop::isMain());
    static const NeverDestroyed<CSSValueToSystemColorMap> colorMap = [] {
        CSSValueToSystemColorMap map;
        for (bool useDarkAppearance : { false, true }) {
            for (bool useElevatedUserInterfaceLevel : { false, true }) {
                LocalCurrentTraitCollection localTraitCollection(useDarkAppearance, useElevatedUserInterfaceLevel);
                for (auto& cssValueSystemColorInformation : cssValueSystemColorInformationList()) {
                    if (auto color = systemColorFromCSSValueSystemColorInformation(cssValueSystemColorInformation, useDarkAppearance))
                        map.add(CSSValueKey { cssValueSystemColorInformation.cssValueID, useDarkAppearance, useElevatedUserInterfaceLevel }, WTFMove(*color));
                }
            }
        }
        return map;
    }();
    return colorMap;
}

void RenderThemeIOS::setCSSValueToSystemColorMap(CSSValueToSystemColorMap&& colorMap)
{
    globalCSSValueToSystemColorMap() = WTFMove(colorMap);
}

void RenderThemeIOS::setFocusRingColor(const Color& color)
{
    cachedFocusRingColor() = color;
}

void RenderThemeIOS::setInsertionPointColor(const Color& color)
{
    cachedInsertionPointColor() = color;
}

Color RenderThemeIOS::systemColor(CSSValueID cssValueID, OptionSet<StyleColorOptions> options) const
{
    const bool forVisitedLink = options.contains(StyleColorOptions::ForVisitedLink);

    // The system color cache below can't handle visited links. The only color value
    // that cares about visited links is CSSValueWebkitLink, so handle it here by
    // calling through to RenderTheme's base implementation.
    if (forVisitedLink && cssValueID == CSSValueWebkitLink)
        return RenderTheme::systemColor(cssValueID, options);

    ASSERT(!forVisitedLink);

    auto& cache = colorCache(options);
    auto it = cache.systemStyleColors.find(cssValueID);
    if (it != cache.systemStyleColors.end())
        return it->value;

    auto color = [this, cssValueID, options]() -> Color {
        const bool useDarkAppearance = options.contains(StyleColorOptions::UseDarkAppearance);
        const bool useElevatedUserInterfaceLevel = options.contains(StyleColorOptions::UseElevatedUserInterfaceLevel);
        if (!globalCSSValueToSystemColorMap().isEmpty()) {
            auto it = globalCSSValueToSystemColorMap().find(CSSValueKey { cssValueID, useDarkAppearance, useElevatedUserInterfaceLevel });
            if (it == globalCSSValueToSystemColorMap().end())
                return RenderTheme::systemColor(cssValueID, options);
            return it->value.semanticColor();
        }
        auto color = systemColorFromCSSValueID(cssValueID, useDarkAppearance, useElevatedUserInterfaceLevel);
        if (color)
            return *color;
        return RenderTheme::systemColor(cssValueID, options);
    }();

    cache.systemStyleColors.add(cssValueID, color);
    return color;
}

Color RenderThemeIOS::pictureFrameColor(const RenderObject& buttonRenderer)
{
    return buttonRenderer.style().visitedDependentColor(CSSPropertyBorderTopColor);
}

#if ENABLE(ATTACHMENT_ELEMENT)

RenderThemeCocoa::IconAndSize RenderThemeIOS::iconForAttachment(const String& fileName, const String& attachmentType, const String& title)
{
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    auto documentInteractionController = adoptNS([PAL::allocUIDocumentInteractionControllerInstance() init]);
ALLOW_DEPRECATED_DECLARATIONS_END

    [documentInteractionController setName:fileName.isEmpty() ? title.createNSString().get() : fileName.createNSString().get()];

    if (!attachmentType.isEmpty()) {
        String UTI;
        if (isDeclaredUTI(attachmentType))
            UTI = attachmentType;
        else
            UTI = UTIFromMIMEType(attachmentType);

#if PLATFORM(IOS) || PLATFORM(VISION)
        [documentInteractionController setUTI:UTI.createNSString().get()];
#endif
    }

    RetainPtr<UIImage> result;
#if PLATFORM(IOS) || PLATFORM(VISION)
    NSArray *icons = [documentInteractionController icons];
    if (!icons.count)
        return IconAndSize { nil, FloatSize() };

    result = icons.lastObject;

    BOOL useHeightForClosestMatch = [result size].height > [result size].width;
    CGFloat bestMatchRatio = -1;

    for (UIImage *icon in icons) {
        CGFloat iconSize = useHeightForClosestMatch ? icon.size.height : icon.size.width;

        CGFloat matchRatio = (attachmentIconSize / iconSize) - 1.0f;
        if (matchRatio < 0.3f) {
            matchRatio = CGFAbs(matchRatio);
            if ((bestMatchRatio == -1) || (matchRatio < bestMatchRatio)) {
                result = icon;
                bestMatchRatio = matchRatio;
            }
        }
    }
#endif
    CGFloat iconAspect = [result size].width / [result size].height;
    auto size = largestRectWithAspectRatioInsideRect(iconAspect, FloatRect(0, 0, attachmentIconSize, attachmentIconSize)).size();

    return IconAndSize { result, size };
}

LayoutSize RenderThemeIOS::attachmentIntrinsicSize(const RenderAttachment&) const
{
    return LayoutSize(FloatSize(attachmentSize) * attachmentDynamicTypeScaleFactor());
}

static void paintAttachmentIcon(GraphicsContext& context, AttachmentLayout& info)
{
    RefPtr<Image> iconImage;
    if (info.thumbnailIcon)
        iconImage = info.thumbnailIcon;
    else if (info.icon)
        iconImage = info.icon;
    
    context.drawImage(*iconImage, info.iconRect);
}

static void paintAttachmentProgress(GraphicsContext& context, AttachmentLayout& info)
{
    GraphicsContextStateSaver saver(context);

    context.setStrokeThickness(attachmentProgressBorderThickness);
    context.setStrokeColor(attachmentProgressColor);
    context.setFillColor(attachmentProgressColor);
    context.strokeEllipse(info.progressRect);

    FloatPoint center = info.progressRect.center();

    Path progressPath;
    progressPath.moveTo(center);
    progressPath.addLineTo(FloatPoint(center.x(), info.progressRect.y()));
    progressPath.addArc(center, info.progressRect.width() / 2, -piOverTwoDouble, info.progress * 2 * std::numbers::pi - piOverTwoDouble, RotationDirection::Counterclockwise);
    progressPath.closeSubpath();
    context.fillPath(progressPath);
}

static Path attachmentBorderPath(AttachmentLayout& info)
{
    auto insetAttachmentRect = info.attachmentRect;
    insetAttachmentRect.inflate(-attachmentBorderThickness / 2);

    Path borderPath;
    borderPath.addRoundedRect(insetAttachmentRect, FloatSize(attachmentBorderRadius, attachmentBorderRadius));
    return borderPath;
}

static void paintAttachmentBorder(GraphicsContext& context, Path& borderPath)
{
    context.setStrokeColor(attachmentBorderColor);
    context.setStrokeThickness(attachmentBorderThickness);
    context.strokePath(borderPath);
}

bool RenderThemeIOS::paintAttachment(const RenderObject& renderer, const PaintInfo& paintInfo, const IntRect& paintRect)
{
    auto* attachment = dynamicDowncast<RenderAttachment>(renderer);
    if (!attachment)
        return false;

    if (attachment->paintWideLayoutAttachmentOnly(paintInfo, paintRect.location()))
        return true;

    AttachmentLayout info(*attachment);

    GraphicsContext& context = paintInfo.context();
    GraphicsContextStateSaver saver(context);

    context.translate(toFloatSize(paintRect.location()));

    if (attachment->shouldDrawBorder()) {
        auto borderPath = attachmentBorderPath(info);
        paintAttachmentBorder(context, borderPath);
        context.clipPath(borderPath);
    }

    context.translate(FloatSize(0, info.contentYOrigin));

    if (info.hasProgress)
        paintAttachmentProgress(context, info);
    else if (info.icon || info.thumbnailIcon)
        paintAttachmentIcon(context, info);

    paintAttachmentText(context, &info);

    return true;
}

String RenderThemeIOS::attachmentStyleSheet() const
{
    ASSERT(DeprecatedGlobalSettings::attachmentElementEnabled());
    return "attachment { appearance: auto; color: -apple-system-blue; }"_s;
}

#endif // ENABLE(ATTACHMENT_ELEMENT)

#if PLATFORM(WATCHOS)

String RenderThemeIOS::extraDefaultStyleSheet()
{
    return "* { -webkit-text-size-adjust: auto; -webkit-hyphens: auto !important; }"_s;
}

#endif

#if USE(SYSTEM_PREVIEW)
void RenderThemeIOS::paintSystemPreviewBadge(Image& image, const PaintInfo& paintInfo, const FloatRect& rect)
{
    paintInfo.context().drawSystemImage(ARKitBadgeSystemImage::create(image), rect);
}
#endif

constexpr auto checkboxRadioBorderWidth = 1.5f;
constexpr auto checkboxRadioBorderDisabledOpacity = 0.3f;

Color RenderThemeIOS::checkboxRadioBorderColor(OptionSet<ControlStyle::State> states, OptionSet<StyleColorOptions> styleColorOptions)
{
    auto defaultBorderColor = systemColor(CSSValueAppleSystemSecondaryLabel, styleColorOptions);

    if (!states.contains(ControlStyle::State::Enabled))
        return defaultBorderColor.colorWithAlphaMultipliedBy(checkboxRadioBorderDisabledOpacity);

    if (states.contains(ControlStyle::State::Pressed))
        return defaultBorderColor.colorWithAlphaMultipliedBy(pressedStateOpacity);

    return defaultBorderColor;
}

Color RenderThemeIOS::checkboxRadioBackgroundColor(const RenderStyle& style, OptionSet<ControlStyle::State> states, OptionSet<StyleColorOptions> styleColorOptions)
{
    bool isEmpty = !states.containsAny({ ControlStyle::State::Checked, ControlStyle::State::Indeterminate });
    bool isEnabled = states.contains(ControlStyle::State::Enabled);
    bool isPressed = states.contains(ControlStyle::State::Pressed);

    if (PAL::currentUserInterfaceIdiomIsVision()) {
        if (!isEnabled)
            return systemColor(isEmpty ? CSSValueWebkitControlBackground : CSSValueAppleSystemOpaqueTertiaryFill, styleColorOptions);

        if (isPressed)
            return isEmpty ? Color(DisplayP3<float> { 0.773, 0.773, 0.773 }) : Color(DisplayP3<float> { 0.067, 0.38, 0.953 });

        return isEmpty ? Color(DisplayP3<float> { 0.835, 0.835, 0.835 }) : Color(DisplayP3<float> { 0.203, 0.47, 0.964 });
    }

    if (!isEnabled)
        return systemColor(isEmpty ? CSSValueWebkitControlBackground : CSSValueAppleSystemOpaqueTertiaryFill, styleColorOptions);

    auto enabledBackgroundColor = isEmpty ? systemColor(CSSValueWebkitControlBackground, styleColorOptions) : controlTintColor(style, styleColorOptions);
    if (isPressed)
        return enabledBackgroundColor.colorWithAlphaMultipliedBy(pressedStateOpacity);

    return enabledBackgroundColor;
}

RefPtr<Gradient> RenderThemeIOS::checkboxRadioBackgroundGradient(const FloatRect& rect, OptionSet<ControlStyle::State> states)
{
    bool isPressed = states.contains(ControlStyle::State::Pressed);
    if (isPressed)
        return nullptr;

    bool isEmpty = !states.containsAny({ ControlStyle::State::Checked, ControlStyle::State::Indeterminate });
    auto gradient = Gradient::create(Gradient::LinearData { rect.minXMinYCorner(), rect.maxXMaxYCorner() }, { ColorInterpolationMethod::SRGB { }, AlphaPremultiplication::Unpremultiplied });
    gradient->addColorStop({ 0.0f, DisplayP3<float> { 0, 0, 0, isEmpty ? 0.05f : 0.125f }});
    gradient->addColorStop({ 1.0f, DisplayP3<float> { 0, 0, 0, 0 }});
    return gradient;
}

Color RenderThemeIOS::checkboxRadioIndicatorColor(OptionSet<ControlStyle::State> states, OptionSet<StyleColorOptions> styleColorOptions)
{
    if (!states.contains(ControlStyle::State::Enabled))
        return systemColor(CSSValueAppleSystemTertiaryLabel, styleColorOptions);

    Color enabledIndicatorColor = systemColor(CSSValueAppleSystemLabel, styleColorOptions | StyleColorOptions::UseDarkAppearance);
    if (states.contains(ControlStyle::State::Pressed))
        return enabledIndicatorColor.colorWithAlphaMultipliedBy(pressedStateOpacity);

    return enabledIndicatorColor;
}

void RenderThemeIOS::paintCheckboxRadioInnerShadow(const PaintInfo& paintInfo, const FloatRoundedRect& roundedRect, OptionSet<ControlStyle::State> states)
{
    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver { context };

    if (auto gradient = checkboxRadioBackgroundGradient(roundedRect.rect(), states)) {
        context.setFillGradient(*gradient);

        Path path;
        path.addRoundedRect(roundedRect);
        context.fillPath(path);
    }

    const FloatSize innerShadowOffset { 2, 2 };
    constexpr auto innerShadowBlur = 3.0f;

    bool isEmpty = !states.containsAny({ ControlStyle::State::Checked, ControlStyle::State::Indeterminate });
    auto firstShadowColor = DisplayP3<float> { 0, 0, 0, isEmpty ? 0.05f : 0.1f };
    context.setDropShadow({ innerShadowOffset, innerShadowBlur, firstShadowColor, ShadowRadiusMode::Default });
    context.setFillColor(Color::black);

    Path innerShadowPath;
    FloatRect innerShadowRect = roundedRect.rect();
    innerShadowRect.inflate(std::max<float>(innerShadowOffset.width(), innerShadowOffset.height()) + innerShadowBlur);
    innerShadowPath.addRect(innerShadowRect);

    FloatRoundedRect innerShadowHoleRect = roundedRect;
    // FIXME: This is not from the spec; but without it we get antialiasing fringe from the fill; we need a better solution.
    innerShadowHoleRect.inflate(0.5);
    innerShadowPath.addRoundedRect(innerShadowHoleRect);

    context.setFillRule(WindRule::EvenOdd);
    context.fillPath(innerShadowPath);

    constexpr auto secondShadowColor = DisplayP3<float> { 1, 1, 1, 0.5f };
    context.setDropShadow({ FloatSize { 0, 0 }, 1, secondShadowColor, ShadowRadiusMode::Default });

    context.fillPath(innerShadowPath);
}

bool RenderThemeIOS::paintCheckbox(const RenderObject& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (box.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::paintCheckbox(box, paintInfo, rect);
#endif

    bool isVision = PAL::currentUserInterfaceIdiomIsVision();

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver { context };

    constexpr auto checkboxHeight = 16.0f;
    constexpr auto checkboxCornerRadius = 5.0f;

    FloatRoundedRect checkboxRect(rect, FloatRoundedRect::Radii(checkboxCornerRadius * rect.height() / checkboxHeight));

    auto controlStates = extractControlStyleStatesForRenderer(box);
    auto styleColorOptions = box.styleColorOptions();

    auto backgroundColor = checkboxRadioBackgroundColor(box.style(), controlStates, styleColorOptions);

    bool checked = controlStates.contains(ControlStyle::State::Checked);
    bool indeterminate = controlStates.contains(ControlStyle::State::Indeterminate);
    bool empty = !checked && !indeterminate;

    if (empty) {
        Path path;
        path.addRoundedRect(checkboxRect);
        if (!isVision) {
            context.setStrokeColor(checkboxRadioBorderColor(controlStates, styleColorOptions));
            context.setStrokeThickness(checkboxRadioBorderWidth * 2);
            context.setStrokeStyle(StrokeStyle::SolidStroke);
        }
            
        context.setFillColor(backgroundColor);
        context.clipPath(path);
        context.drawPath(path);

        if (isVision)
            paintCheckboxRadioInnerShadow(paintInfo, checkboxRect, controlStates);

        return false;
    }

    context.fillRoundedRect(checkboxRect, backgroundColor);

    if (isVision) {
        context.clipRoundedRect(checkboxRect);
        paintCheckboxRadioInnerShadow(paintInfo, checkboxRect, controlStates);
    }

    Path path;
    if (indeterminate) {
        const FloatSize indeterminateBarRoundingRadii(1.25f, 1.25f);
        constexpr float indeterminateBarPadding = 2.5f;
        float height = 0.12f * rect.height();

        FloatRect indeterminateBarRect(rect.x() + indeterminateBarPadding, rect.center().y() - height / 2.0f, rect.width() - indeterminateBarPadding * 2, height);
        path.addRoundedRect(indeterminateBarRect, indeterminateBarRoundingRadii);
    } else {
        path.moveTo({ 28.174f, 68.652f });
        path.addBezierCurveTo({ 31.006f, 68.652f }, { 33.154f, 67.578f }, { 34.668f, 65.332f });
        path.addLineTo({ 70.02f, 11.28f });
        path.addBezierCurveTo({ 71.094f, 9.62f }, { 71.582f, 8.107f }, { 71.582f, 6.642f });
        path.addBezierCurveTo({ 71.582f, 2.784f }, { 68.652f, 0.001f }, { 64.697f, 0.001f });
        path.addBezierCurveTo({ 62.012f, 0.001f }, { 60.352f, 0.978f }, { 58.691f, 3.565f });
        path.addLineTo({ 28.027f, 52.1f });
        path.addLineTo({ 12.354f, 32.52f });
        path.addBezierCurveTo({ 10.84f, 30.664f }, { 9.18f, 29.834f }, { 6.884f, 29.834f });
        path.addBezierCurveTo({ 2.882f, 29.834f }, { 0.0f, 32.666f }, { 0.0f, 36.572f });
        path.addBezierCurveTo({ 0.0f, 38.282f }, { 0.537f, 39.795f }, { 2.002f, 41.504f });
        path.addLineTo({ 21.826f, 65.625f });
        path.addBezierCurveTo({ 23.536f, 67.675f }, { 25.536f, 68.652f }, { 28.174f, 68.652f });

        const FloatSize checkmarkSize(72.0f, 69.0f);
        float scale = (0.65f * rect.width()) / checkmarkSize.width();

        AffineTransform transform;
        transform.translate(rect.center() - (checkmarkSize * scale * 0.5f));
        transform.scale(scale);
        path.transform(transform);
    }

    context.setFillColor(checkboxRadioIndicatorColor(controlStates, styleColorOptions));
    context.fillPath(path);

    return false;
}

bool RenderThemeIOS::paintRadio(const RenderObject& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (box.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::paintRadio(box, paintInfo, rect);
#endif

    bool isVision = PAL::currentUserInterfaceIdiomIsVision();

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    auto controlStates = extractControlStyleStatesForRenderer(box);
    auto styleColorOptions = box.styleColorOptions();

    auto backgroundColor = checkboxRadioBackgroundColor(box.style(), controlStates, styleColorOptions);

    FloatRoundedRect radioRect { rect, FloatRoundedRect::Radii(rect.width() / 2, rect.height() / 2) };

    if (controlStates.contains(ControlStyle::State::Checked)) {
        context.setFillColor(backgroundColor);
        context.fillEllipse(rect);

        if (isVision) {
            context.clipRoundedRect(radioRect);
            paintCheckboxRadioInnerShadow(paintInfo, radioRect, controlStates);
        }

        // The inner circle is 6 / 14 the size of the surrounding circle,
        // leaving 8 / 14 around it. (8 / 14) / 2 = 2 / 7.
        constexpr float innerInverseRatio = 2 / 7.0f;

        FloatRect innerCircleRect(rect);
        innerCircleRect.inflateX(-innerCircleRect.width() * innerInverseRatio);
        innerCircleRect.inflateY(-innerCircleRect.height() * innerInverseRatio);

        context.setFillColor(checkboxRadioIndicatorColor(controlStates, styleColorOptions));
        context.fillEllipse(innerCircleRect);
    } else {
        Path path;
        path.addEllipseInRect(rect);
        if (!isVision) {
            context.setStrokeColor(checkboxRadioBorderColor(controlStates, styleColorOptions));
            context.setStrokeThickness(checkboxRadioBorderWidth * 2);
            context.setStrokeStyle(StrokeStyle::SolidStroke);
        }
        context.setFillColor(backgroundColor);
        context.clipPath(path);
        context.drawPath(path);

        if (isVision)
            paintCheckboxRadioInnerShadow(paintInfo, radioRect, controlStates);
    }

    return false;
}

bool RenderThemeIOS::supportsMeter(StyleAppearance appearance) const
{
    return appearance == StyleAppearance::Meter;
}

bool RenderThemeIOS::paintMeter(const RenderObject& renderer, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (renderer.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::paintMeter(renderer, paintInfo, rect);
#endif

    auto* renderMeter = dynamicDowncast<RenderMeter>(renderer);
    if (!renderMeter)
        return true;

    RefPtr element = renderMeter->meterElement();

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    auto styleColorOptions = renderer.styleColorOptions();
    auto isHorizontalWritingMode = renderer.writingMode().isHorizontal();

    float cornerRadius = std::min(rect.width(), rect.height()) / 2.0f;
    FloatRoundedRect roundedFillRect(rect, FloatRoundedRect::Radii(cornerRadius));
    context.fillRoundedRect(roundedFillRect, systemColor(CSSValueWebkitControlBackground, styleColorOptions));

    roundedFillRect.inflateWithRadii(-nativeControlBorderInlineSize);
    context.fillRoundedRect(roundedFillRect, systemColor(CSSValueAppleSystemOpaqueTertiaryFill, styleColorOptions));

    context.clipRoundedRect(roundedFillRect);

    FloatRect fillRect(roundedFillRect.rect());

    auto fillRectInlineSize = isHorizontalWritingMode ? fillRect.width() : fillRect.height();
    FloatSize gaugeRegionPosition(fillRectInlineSize * (element->valueRatio() - 1), 0);

    if (!isHorizontalWritingMode)
        gaugeRegionPosition = gaugeRegionPosition.transposedSize();

    if (renderer.writingMode().isInlineFlipped())
        gaugeRegionPosition = -gaugeRegionPosition;

    fillRect.move(gaugeRegionPosition);
    roundedFillRect.setRect(fillRect);

    switch (element->gaugeRegion()) {
    case HTMLMeterElement::GaugeRegionOptimum:
        context.fillRoundedRect(roundedFillRect, systemColor(CSSValueAppleSystemGreen, styleColorOptions));
        break;
    case HTMLMeterElement::GaugeRegionSuboptimal:
        context.fillRoundedRect(roundedFillRect, systemColor(CSSValueAppleSystemYellow, styleColorOptions));
        break;
    case HTMLMeterElement::GaugeRegionEvenLessGood:
        context.fillRoundedRect(roundedFillRect, systemColor(CSSValueAppleSystemRed, styleColorOptions));
        break;
    }

    return false;
}

bool RenderThemeIOS::paintListButton(const RenderObject& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (box.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::paintListButton(box, paintInfo, rect);
#endif

    auto& style = box.style();
    if (style.hasContent() || style.hasUsedContentNone())
        return false;

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    float paddingTop = Style::evaluate(style.paddingTop(), rect.height());
    float paddingRight = Style::evaluate(style.paddingRight(), rect.width());
    float paddingBottom = Style::evaluate(style.paddingBottom(), rect.height());
    float paddingLeft = Style::evaluate(style.paddingLeft(), rect.width());

    FloatRect indicatorRect = rect;
    indicatorRect.move(paddingLeft, paddingTop);
    indicatorRect.contract(paddingLeft + paddingRight, paddingTop + paddingBottom);

    Path path;
    path.moveTo({ 35.48, 38.029 });
    path.addBezierCurveTo({ 36.904, 38.029 }, { 38.125, 37.5 }, { 39.223, 36.361 });
    path.addLineTo({ 63.352, 11.987 });
    path.addBezierCurveTo({ 64.206, 11.092 }, { 64.695, 9.993 }, { 64.695, 8.691 });
    path.addBezierCurveTo({ 64.695, 6.046 }, { 62.579, 3.971 }, { 59.975, 3.971 });
    path.addBezierCurveTo({ 58.714, 3.971 }, { 57.493, 4.5 }, { 56.557, 5.436 });
    path.addLineTo({ 35.52, 26.839 });
    path.addLineTo({ 14.443, 5.436 });
    path.addBezierCurveTo({ 13.507, 4.5 }, { 12.327, 3.971 }, { 10.984, 3.971 });
    path.addBezierCurveTo({ 8.38, 3.971 }, { 6.305, 6.046 }, { 6.305, 8.691 });
    path.addBezierCurveTo({ 6.305, 9.993 }, { 6.753, 11.092 }, { 7.648, 11.987 });
    path.addLineTo({ 31.777, 36.36 });
    path.addBezierCurveTo({ 32.916, 37.499 }, { 34.096, 38.028 }, { 35.48, 38.028 });

    const FloatSize indicatorSize(71.0f, 42.0f);
    float scale = indicatorRect.width() / indicatorSize.width();

    AffineTransform transform;
    transform.translate(rect.center() - (indicatorSize * scale * 0.5f));
    transform.scale(scale);
    path.transform(transform);

    context.setFillColor(controlTintColor(style, box.styleColorOptions()));
    context.fillPath(path);

    return false;
}

void RenderThemeIOS::paintSliderTicks(const RenderObject& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
    RefPtr input = dynamicDowncast<HTMLInputElement>(box.node());
    if (!input || !input->isRangeControl())
        return;

    auto dataList = input->dataList();
    if (!dataList)
        return;

    double min = input->minimum();
    double max = input->maximum();
    if (min >= max)
        return;

    constexpr int tickWidth = 2;
    constexpr int tickHeight = 8;
    constexpr int tickCornerRadius = 1;

    FloatRect tickRect;
    FloatRoundedRect::Radii tickCornerRadii(tickCornerRadius);

    bool isHorizontal = box.style().usedAppearance() == StyleAppearance::SliderHorizontal;
    if (isHorizontal) {
        tickRect.setWidth(tickWidth);
        tickRect.setHeight(tickHeight);
        tickRect.setY(rect.center().y() - tickRect.height() / 2.0f);
    } else {
        tickRect.setWidth(tickHeight);
        tickRect.setHeight(tickWidth);
        tickRect.setX(rect.center().x() - tickRect.width() / 2.0f);
    }

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    auto value = input->valueAsNumber();
    auto deviceScaleFactor = box.document().deviceScaleFactor();
    auto styleColorOptions = box.styleColorOptions();

    bool isInlineFlipped = (!isHorizontal && box.writingMode().isHorizontal()) || box.writingMode().isInlineFlipped();
    for (auto& optionElement : dataList->suggestions()) {
        if (auto optionValue = input->listOptionValueAsDouble(optionElement)) {
            auto tickFraction = (*optionValue - min) / (max - min);
            auto tickRatio = isInlineFlipped ? 1.0 - tickFraction : tickFraction;
            if (isHorizontal)
                tickRect.setX(rect.x() + tickRatio * (rect.width() - tickRect.width()));
            else
                tickRect.setY(rect.y() + tickRatio * (rect.height() - tickRect.height()));

            FloatRoundedRect roundedTickRect(snapRectToDevicePixels(LayoutRect(tickRect), deviceScaleFactor), tickCornerRadii);
            context.fillRoundedRect(roundedTickRect, (value >= *optionValue) ? controlTintColor(box.style(), styleColorOptions) : systemColor(CSSValueAppleSystemOpaqueSeparator, styleColorOptions));
        }
    }
}

void RenderThemeIOS::paintColorWellDecorations(const RenderObject& renderer, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (renderer.settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::paintColorWellDecorations(renderer, paintInfo, rect);
        return;
    }
#else
    UNUSED_PARAM(renderer);
#endif

    constexpr int strokeThickness = 3;
    constexpr std::array colorStops {
        DisplayP3<float> { 1, 1, 0, 1 },
        DisplayP3<float> { 1, 0.5, 0, 1 },
        DisplayP3<float> { 1, 0, 0, 1 },
        DisplayP3<float> { 1, 0, 1, 1 },
        DisplayP3<float> { 0, 0, 1, 1 },
        DisplayP3<float> { 0, 1, 1, 1 },
        DisplayP3<float> { 0, 1, 0, 1 },
        DisplayP3<float> { 0.63, 0.88, 0.03, 1 },
        DisplayP3<float> { 1, 1, 0, 1 }
    };
    constexpr int numColorStops = std::size(colorStops);

    auto gradient = Gradient::create(Gradient::ConicData { rect.center(), 0 }, { ColorInterpolationMethod::SRGB { }, AlphaPremultiplication::Unpremultiplied });
    for (int i = 0; i < numColorStops; ++i)
        gradient->addColorStop({ i * 1.0f / (numColorStops - 1), colorStops[i] });

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    FloatRect strokeRect = rect;
    strokeRect.inflate(-strokeThickness / 2.0f);

    context.setStrokeThickness(strokeThickness);
    context.setStrokeStyle(StrokeStyle::SolidStroke);
    context.setStrokeGradient(WTFMove(gradient));
    context.strokeEllipse(strokeRect);
}

void RenderThemeIOS::adjustSearchFieldDecorationPartStyle(RenderStyle& style, const Element* element) const
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (element && element->document().settings().formControlRefreshEnabled()) {
        RenderThemeCocoa::adjustSearchFieldDecorationPartStyle(style, element);
        return;
    }
#endif

    if (!element)
        return;

    constexpr auto searchFieldDecorationEmSize = 1.0f;
    constexpr auto searchFieldDecorationMargin = 4_css_px;

    CSSToLengthConversionData conversionData(style, nullptr, nullptr, nullptr);

    auto emSize = CSSPrimitiveValue::create(searchFieldDecorationEmSize, CSSUnitType::CSS_EM);
    auto size = Style::PreferredSize::Fixed { emSize->resolveAsLength<float>(conversionData) };

    style.setWidth(size);
    style.setHeight(size);
    style.setMarginEnd(searchFieldDecorationMargin);
}

bool RenderThemeIOS::paintSearchFieldDecorationPart(const RenderObject& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
#if ENABLE(FORM_CONTROL_REFRESH)
    if (box.settings().formControlRefreshEnabled())
        return RenderThemeCocoa::paintSearchFieldDecorationPart(box, paintInfo, rect);
#endif

    auto& context = paintInfo.context();
    GraphicsContextStateSaver stateSaver(context);

    const FloatSize glyphSize(73.0f, 73.0f);

    Path glyphPath;
    glyphPath.moveTo({ 29.6875f, 59.375f });
    glyphPath.addBezierCurveTo({ 35.9863f, 59.375f }, { 41.7969f, 57.422f }, { 46.6309f, 54.0528f });
    glyphPath.addLineTo({ 63.9649f, 71.3868f });
    glyphPath.addBezierCurveTo({ 64.8926f, 72.3145f }, { 66.1133f, 72.754f }, { 67.3829f, 72.754f });
    glyphPath.addBezierCurveTo({ 70.1172f, 72.754f }, { 72.1191f, 70.6544f }, { 72.1191f, 67.9688f });
    glyphPath.addBezierCurveTo({ 72.1191f, 66.6993f }, { 71.6797f, 65.4786f }, { 70.7519f, 64.5508f });
    glyphPath.addLineTo({ 53.5644f, 47.3145f });
    glyphPath.addBezierCurveTo({ 57.2266f, 42.3829f }, { 59.375f, 36.2793f }, { 59.375f, 29.6875f });
    glyphPath.addBezierCurveTo({ 59.375f, 13.3301f }, { 46.045f, 0.0f }, { 29.6875f, 0.0f });
    glyphPath.addBezierCurveTo({ 13.3301f, 0.0f }, { 0.0f, 13.3301f }, { 0.0f, 29.6875f });
    glyphPath.addBezierCurveTo({ 0.0f, 46.045f }, { 13.33f, 59.375f }, { 29.6875f, 59.375f });
    glyphPath.moveTo({ 29.6875f, 52.0997f });
    glyphPath.addBezierCurveTo({ 17.4316f, 52.0997f }, { 7.2754f, 41.9434f }, { 7.2754f, 29.6875f });
    glyphPath.addBezierCurveTo({ 7.2754f, 17.3829f }, { 17.4316f, 7.2754f }, { 29.6875f, 7.2754f });
    glyphPath.addBezierCurveTo({ 41.9922f, 7.2754f }, { 52.1f, 17.3829f }, { 52.1f, 29.6875f });
    glyphPath.addBezierCurveTo({ 52.1f, 41.9435f }, { 41.9922f, 52.0997f }, { 29.6875f, 52.0997f });

    FloatRect paintRect(rect);
    float scale = paintRect.width() / glyphSize.width();

    AffineTransform transform;
    transform.translate(paintRect.center() - (glyphSize * scale * 0.5f));
    transform.scale(scale);
    glyphPath.transform(transform);

    context.setFillColor(systemColor(CSSValueAppleSystemSecondaryLabel, box.styleColorOptions()));
    context.fillPath(glyphPath);

    return false;
}

void RenderThemeIOS::adjustSearchFieldResultsDecorationPartStyle(RenderStyle& style, const Element* element) const
{
    adjustSearchFieldDecorationPartStyle(style, element);
}

bool RenderThemeIOS::paintSearchFieldResultsDecorationPart(const RenderBox& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
    return paintSearchFieldDecorationPart(box, paintInfo, rect);
}

void RenderThemeIOS::adjustSearchFieldResultsButtonStyle(RenderStyle& style, const Element* element) const
{
    adjustSearchFieldDecorationPartStyle(style, element);
}

bool RenderThemeIOS::paintSearchFieldResultsButton(const RenderBox& box, const PaintInfo& paintInfo, const FloatRect& rect)
{
    return paintSearchFieldDecorationPart(box, paintInfo, rect);
}

} // namespace WebCore

#endif //PLATFORM(IOS_FAMILY)
