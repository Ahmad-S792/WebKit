/*
 * Copyright (C) 2011-2025 Apple Inc. All rights reserved.
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
#import "NodeHTMLConverter.h"

#import "ArchiveResource.h"
#import "BoundaryPointInlines.h"
#import "CSSColorValue.h"
#import "CSSComputedStyleDeclaration.h"
#import "CSSPrimitiveValue.h"
#import "CSSSerializationContext.h"
#import "CachedImage.h"
#import "CharacterData.h"
#import "ColorCocoa.h"
#import "ColorMac.h"
#import "CommonAtomStrings.h"
#import "ComposedTreeIterator.h"
#import "ContainerNodeInlines.h"
#import "Document.h"
#import "DocumentLoader.h"
#import "Editing.h"
#import "ElementChildIteratorInlines.h"
#import "ElementInlines.h"
#import "ElementRareData.h"
#import "ElementTraversal.h"
#import "File.h"
#import "FontCascade.h"
#import "FrameLoader.h"
#import "HTMLAttachmentElement.h"
#import "HTMLConverter.h"
#import "HTMLElement.h"
#import "HTMLFrameElement.h"
#import "HTMLIFrameElement.h"
#import "HTMLImageElement.h"
#import "HTMLInputElement.h"
#import "HTMLMetaElement.h"
#import "HTMLNames.h"
#import "HTMLOListElement.h"
#import "HTMLTableCellElement.h"
#import "HTMLTextAreaElement.h"
#import "ImageAdapter.h"
#import "LoaderNSURLExtras.h"
#import "LocalFrame.h"
#import "LocalizedStrings.h"
#import "NodeName.h"
#import "Quirks.h"
#import "RenderImage.h"
#import "RenderText.h"
#import "StyleExtractor.h"
#import "StyleProperties.h"
#import "StyledElement.h"
#import "TextIterator.h"
#import "VisibleSelection.h"
#import "WebContentReader.h"
#import "WebCoreTextAttachment.h"
#import "markup.h"
#import <objc/runtime.h>
#import <pal/spi/cocoa/NSAttributedStringSPI.h>
#import <wtf/ASCIICType.h>
#import <wtf/TZoneMallocInlines.h>
#import <wtf/text/MakeString.h>
#import <wtf/text/ParsingUtilities.h>
#import <wtf/text/StringBuilder.h>
#import <wtf/text/StringToIntegerConversion.h>

#if ENABLE(DATA_DETECTION)
#import "DataDetection.h"
#endif

#if ENABLE(MULTI_REPRESENTATION_HEIC)
#import "PlatformNSAdaptiveImageGlyph.h"
#endif

#if PLATFORM(IOS_FAMILY)
#import "UIFoundationSoftLink.h"
#import "WAKAppKitStubs.h"
#import <pal/ios/UIKitSoftLink.h>
#import <pal/spi/ios/UIKitSPI.h>
#endif

using namespace WebCore;
using namespace HTMLNames;

#if PLATFORM(IOS_FAMILY)

enum {
    NSEnterCharacter                = 0x0003,
    NSBackspaceCharacter            = 0x0008,
    NSTabCharacter                  = 0x0009,
    NSNewlineCharacter              = 0x000a,
    NSFormFeedCharacter             = 0x000c,
    NSCarriageReturnCharacter       = 0x000d,
    NSBackTabCharacter              = 0x0019,
    NSDeleteCharacter               = 0x007f,
    NSLineSeparatorCharacter        = 0x2028,
    NSParagraphSeparatorCharacter   = 0x2029,
};

@interface NSTextBlock ()
- (void)setWidth:(CGFloat)val type:(NSTextBlockValueType)type forLayer:(NSTextBlockLayer)layer edge:(NSRectEdge)edge;
- (void)setBorderColor:(UIColor *)color forEdge:(NSRectEdge)edge;
@end

#endif

// Additional control Unicode characters
const unichar WebNextLineCharacter = 0x0085;

static const CGFloat defaultFontSize = 12;
static const CGFloat minimumFontSize = 1;

using NodeSet = HashSet<Ref<Node>>;

class HTMLConverterCaches {
    WTF_MAKE_TZONE_ALLOCATED(HTMLConverterCaches);
public:
    String propertyValueForNode(Node&, CSSPropertyID );
    bool floatPropertyValueForNode(Node&, CSSPropertyID, float&);
    Color colorPropertyValueForNode(Node&, CSSPropertyID);

    bool isBlockElement(Element&);
    bool elementHasOwnBackgroundColor(Element&);

    RefPtr<CSSValue> computedStylePropertyForElement(Element&, CSSPropertyID);
    RefPtr<CSSValue> inlineStylePropertyForElement(Element&, CSSPropertyID);

    Node* cacheAncestorsOfStartToBeConverted(const Position&, const Position&);
    bool isAncestorsOfStartToBeConverted(Node& node) const { return m_ancestorsUnderCommonAncestor.contains(&node); }

private:
    HashMap<Element*, std::unique_ptr<WebCore::Style::Extractor>> m_computedStyles;
    NodeSet m_ancestorsUnderCommonAncestor;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(HTMLConverterCaches);

@interface NSTextList (WebCoreNSTextListDetails)
+ (NSDictionary *)_standardMarkerAttributesForAttributes:(NSDictionary *)attrs;
@end

@interface NSObject(WebMessageDocumentSimulation)
+ (void)document:(NSObject **)outDocument attachment:(NSTextAttachment **)outAttachment forURL:(NSURL *)url;
@end

class HTMLConverter {
public:
    explicit HTMLConverter(const SimpleRange&, IgnoreUserSelectNone);
    ~HTMLConverter();

    AttributedString convert();

private:
    Position m_start;
    Position m_end;
    DocumentLoader* m_dataSource { nullptr };

    HashMap<RefPtr<Element>, RetainPtr<NSDictionary>> m_attributesForElements;
    HashMap<RetainPtr<CFTypeRef>, RefPtr<Element>> m_textTableFooters;
    HashMap<RefPtr<Element>, RetainPtr<NSDictionary>> m_aggregatedAttributesForElements;

    UserSelectNoneStateCache m_userSelectNoneStateCache;
    bool m_ignoreUserSelectNoneContent { false };

    RetainPtr<NSMutableAttributedString> _attrStr;
    RetainPtr<NSMutableDictionary> _documentAttrs;
    RetainPtr<NSPresentationIntent> _topPresentationIntent;
    NSInteger _topPresentationIntentIdentity;
    RetainPtr<NSMutableArray> _textLists;
    RetainPtr<NSMutableArray> _textBlocks;
    RetainPtr<NSMutableArray> _textTables;
    RetainPtr<NSMutableArray> _textTableSpacings;
    RetainPtr<NSMutableArray> _textTablePaddings;
    RetainPtr<NSMutableArray> _textTableRows;
    RetainPtr<NSMutableArray> _textTableRowArrays;
    RetainPtr<NSMutableArray> _textTableRowBackgroundColors;
    RetainPtr<NSMutableDictionary> _fontCache;
    RetainPtr<NSMutableArray> _writingDirectionArray;

    CGFloat _defaultTabInterval;
    NSUInteger _domRangeStartIndex;
    NSInteger _quoteLevel;

    std::unique_ptr<HTMLConverterCaches> _caches;

    struct {
        unsigned int isSoft:1;
        unsigned int reachedStart:1;
        unsigned int reachedEnd:1;
        unsigned int hasTrailingNewline:1;
        unsigned int pad:26;
    } _flags;

    RetainPtr<PlatformColor> _colorForElement(Element&, CSSPropertyID);

    void _traverseNode(Node&, unsigned depth, bool embedded);
    void _traverseFooterNode(Element&, unsigned depth);

    NSDictionary *computedAttributesForElement(Element&);
    NSDictionary *attributesForElement(Element&);
    NSDictionary *aggregatedAttributesForAncestors(CharacterData&);
    NSDictionary* aggregatedAttributesForElementAndItsAncestors(Element&);

    Element* _blockLevelElementForNode(Node*);

#if ENABLE(MULTI_REPRESENTATION_HEIC)
    BOOL _addMultiRepresentationHEICAttachmentForImageElement(HTMLImageElement&);
#endif

    void _newParagraphForElement(Element&, NSString *tag, BOOL flag, BOOL suppressTrailingSpace);
    void _newLineForElement(Element&);
    void _newTabForElement(Element&);
    BOOL _addAttachmentForElement(Element&, NSURL *url, BOOL needsParagraph, BOOL usePlaceholder);
    void _addQuoteForElement(Element&, BOOL opening, NSInteger level);
    void _addValue(NSString *value, Element&);
    void _fillInBlock(NSTextBlock *block, Element&, PlatformColor *backgroundColor, CGFloat extraMargin, CGFloat extraPadding, BOOL isTable);
    void _enterBlockquote();
    void _exitBlockquote();

    BOOL _enterElement(Element&, BOOL embedded);
    BOOL _processElement(Element&, NSInteger depth);
    void _exitElement(Element&, NSInteger depth, NSUInteger startIndex);

    void _processHeadElement(Element&);
    void _processMetaElementWithName(NSString *name, NSString *content);

    void _addLinkForElement(Element&, NSRange);
    void _addTableForElement(Element* tableElement);
    void _addTableCellForElement(Element* tableCellElement);
    void _addMarkersToList(NSTextList *list, NSRange range);
    void _processText(Text&);
    void _adjustTrailingNewline();
};

HTMLConverter::HTMLConverter(const SimpleRange& range, IgnoreUserSelectNone treatment)
    : m_start(makeContainerOffsetPosition(range.start))
    , m_end(makeContainerOffsetPosition(range.end))
    , m_userSelectNoneStateCache(ComposedTree)
    , m_ignoreUserSelectNoneContent(treatment == IgnoreUserSelectNone::Yes && !range.start.document().quirks().needsToCopyUserSelectNoneQuirk())
{
    _attrStr = adoptNS([[NSMutableAttributedString alloc] init]);
    _documentAttrs = adoptNS([[NSMutableDictionary alloc] init]);
    _topPresentationIntent = nil;
    _topPresentationIntentIdentity = 0;
    _textLists = adoptNS([[NSMutableArray alloc] init]);
    _textBlocks = adoptNS([[NSMutableArray alloc] init]);
    _textTables = adoptNS([[NSMutableArray alloc] init]);
    _textTableSpacings = adoptNS([[NSMutableArray alloc] init]);
    _textTablePaddings = adoptNS([[NSMutableArray alloc] init]);
    _textTableRows = adoptNS([[NSMutableArray alloc] init]);
    _textTableRowArrays = adoptNS([[NSMutableArray alloc] init]);
    _textTableRowBackgroundColors = adoptNS([[NSMutableArray alloc] init]);
    _fontCache = adoptNS([[NSMutableDictionary alloc] init]);
    _writingDirectionArray = adoptNS([[NSMutableArray alloc] init]);

    _defaultTabInterval = 36;
    _domRangeStartIndex = 0;
    _quoteLevel = 0;

    _flags.isSoft = false;
    _flags.reachedStart = false;
    _flags.reachedEnd = false;

    _caches = makeUnique<HTMLConverterCaches>();
}

HTMLConverter::~HTMLConverter() = default;

AttributedString HTMLConverter::convert()
{
    if (m_start > m_end)
        return { };

    Node* commonAncestorContainer = _caches->cacheAncestorsOfStartToBeConverted(m_start, m_end);
    ASSERT(commonAncestorContainer);

    m_dataSource = commonAncestorContainer->document().frame()->loader().documentLoader();

    Document& document = commonAncestorContainer->document();
    if (auto* body = document.bodyOrFrameset()) {
        if (auto backgroundColor = _colorForElement(*body, CSSPropertyBackgroundColor))
            [_documentAttrs setObject:backgroundColor.get() forKey:NSBackgroundColorDocumentAttribute];
    }

    _domRangeStartIndex = 0;
    _traverseNode(*commonAncestorContainer, 0, false /* embedded */);
    if (_domRangeStartIndex > 0 && _domRangeStartIndex <= [_attrStr length])
        [_attrStr deleteCharactersInRange:NSMakeRange(0, _domRangeStartIndex)];

    return AttributedString::fromNSAttributedStringAndDocumentAttributes(WTFMove(_attrStr), WTFMove(_documentAttrs));
}

#if !PLATFORM(IOS_FAMILY)
static RetainPtr<NSFileWrapper> fileWrapperForURL(DocumentLoader* dataSource, NSURL *URL)
{
    if ([URL isFileURL])
        return adoptNS([[NSFileWrapper alloc] initWithURL:[URL URLByResolvingSymlinksInPath] options:0 error:nullptr]);

    if (dataSource) {
        if (RefPtr<ArchiveResource> resource = dataSource->subresource(URL)) {
            auto wrapper = adoptNS([[NSFileWrapper alloc] initRegularFileWithContents:resource->data().makeContiguous()->createNSData().get()]);
            RetainPtr filename = resource->response().suggestedFilename().createNSString();
            if (!filename || ![filename length])
                filename = suggestedFilenameWithMIMEType(resource->url().createNSURL().get(), resource->mimeType());
            [wrapper setPreferredFilename:filename.get()];
            return wrapper;
        }
    }

    NSCachedURLResponse *cachedResponse = [[NSURLCache sharedURLCache] cachedResponseForRequest:adoptNS([[NSMutableURLRequest alloc] initWithURL:URL]).get()];
    if (cachedResponse) {
        auto wrapper = adoptNS([[NSFileWrapper alloc] initRegularFileWithContents:[cachedResponse data]]);
        [wrapper setPreferredFilename:[[cachedResponse response] suggestedFilename]];
        return wrapper;
    }

    return nil;
}
#endif // !PLATFORM(IOS_FAMILY)

static PlatformFont *_fontForNameAndSize(NSString *fontName, CGFloat size, NSMutableDictionary *cache)
{
    PlatformFont *font = [cache objectForKey:fontName];
#if PLATFORM(IOS_FAMILY)
    if (font)
        return [font fontWithSize:size];

    font = [PlatformFontClass fontWithName:fontName size:size];
#else
    NSFontManager *fontManager = [NSFontManager sharedFontManager];
    if (font) {
        font = [fontManager convertFont:font toSize:size];
        return font;
    }
    font = [fontManager fontWithFamily:fontName traits:0 weight:0 size:size];
#endif
    if (!font) {
#if PLATFORM(IOS_FAMILY)
        NSArray *availableFamilyNames = [PlatformFontClass familyNames];
#else
        NSArray *availableFamilyNames = [fontManager availableFontFamilies];
#endif
        NSRange dividingRange;
        NSRange dividingSpaceRange = [fontName rangeOfString:@" " options:NSBackwardsSearch];
        NSRange dividingDashRange = [fontName rangeOfString:@"-" options:NSBackwardsSearch];
        dividingRange = (0 < dividingSpaceRange.length && 0 < dividingDashRange.length) ? (dividingSpaceRange.location > dividingDashRange.location ? dividingSpaceRange : dividingDashRange) : (0 < dividingSpaceRange.length ? dividingSpaceRange : dividingDashRange);

        while (dividingRange.length > 0) {
            NSString *familyName = [fontName substringToIndex:dividingRange.location];
            if ([availableFamilyNames containsObject:familyName]) {
#if PLATFORM(IOS_FAMILY)
                NSString *faceName = [fontName substringFromIndex:(dividingRange.location + dividingRange.length)];
                NSArray *familyMemberFaceNames = [PlatformFontClass fontNamesForFamilyName:familyName];
                for (NSString *familyMemberFaceName in familyMemberFaceNames) {
                    if ([familyMemberFaceName compare:faceName options:NSCaseInsensitiveSearch] == NSOrderedSame) {
                        font = [PlatformFontClass fontWithName:familyMemberFaceName size:size];
                        break;
                    }
                }
                if (!font && [familyMemberFaceNames count])
                    font = [PlatformFontClass fontWithName:familyName size:size];
#else
                NSArray *familyMemberArray;
                NSString *faceName = [fontName substringFromIndex:(dividingRange.location + dividingRange.length)];
                NSArray *familyMemberArrays = [fontManager availableMembersOfFontFamily:familyName];
                NSEnumerator *familyMemberArraysEnum = [familyMemberArrays objectEnumerator];
                while ((familyMemberArray = [familyMemberArraysEnum nextObject])) {
                    NSString *familyMemberFaceName = [familyMemberArray objectAtIndex:1];
                    if ([familyMemberFaceName compare:faceName options:NSCaseInsensitiveSearch] == NSOrderedSame) {
                        NSFontTraitMask traits = [[familyMemberArray objectAtIndex:3] integerValue];
                        NSInteger weight = [[familyMemberArray objectAtIndex:2] integerValue];
                        font = [fontManager fontWithFamily:familyName traits:traits weight:weight size:size];
                        break;
                    }
                }
                if (!font) {
                    if (0 < [familyMemberArrays count]) {
                        NSArray *familyMemberArray = [familyMemberArrays objectAtIndex:0];
                        NSFontTraitMask traits = [[familyMemberArray objectAtIndex:3] integerValue];
                        NSInteger weight = [[familyMemberArray objectAtIndex:2] integerValue];
                        font = [fontManager fontWithFamily:familyName traits:traits weight:weight size:size];
                    }
                }
#endif
                break;
            } else {
                dividingSpaceRange = [familyName rangeOfString:@" " options:NSBackwardsSearch];
                dividingDashRange = [familyName rangeOfString:@"-" options:NSBackwardsSearch];
                dividingRange = (0 < dividingSpaceRange.length && 0 < dividingDashRange.length) ? (dividingSpaceRange.location > dividingDashRange.location ? dividingSpaceRange : dividingDashRange) : (0 < dividingSpaceRange.length ? dividingSpaceRange : dividingDashRange);
            }
        }
    }
#if PLATFORM(IOS_FAMILY)
    if (!font)
        font = [PlatformFontClass systemFontOfSize:size];
#else
    if (!font)
        font = [NSFont fontWithName:@"Times" size:size];
    if (!font)
        font = [NSFont userFontOfSize:size];
    if (!font)
        font = [fontManager convertFont:WebDefaultFont() toSize:size];
    if (!font)
        font = WebDefaultFont();
#endif
    [cache setObject:font forKey:fontName];

    return font;
}

static NSParagraphStyle *defaultParagraphStyle()
{
    static NeverDestroyed style = [] {
        auto style = adoptNS([[PlatformNSParagraphStyle defaultParagraphStyle] mutableCopy]);
        [style setDefaultTabInterval:36];
        [style setTabStops:@[]];
        return style;
    }();
    return style.get().get();
}

RefPtr<CSSValue> HTMLConverterCaches::computedStylePropertyForElement(Element& element, CSSPropertyID propertyId)
{
    if (propertyId == CSSPropertyInvalid)
        return nullptr;

    auto result = m_computedStyles.add(&element, nullptr);
    if (result.isNewEntry)
        result.iterator->value = makeUnique<WebCore::Style::Extractor>(&element, true);
    auto& computedStyle = *result.iterator->value;
    return computedStyle.propertyValue(propertyId);
}

RefPtr<CSSValue> HTMLConverterCaches::inlineStylePropertyForElement(Element& element, CSSPropertyID propertyId)
{
    if (propertyId == CSSPropertyInvalid)
        return nullptr;

    RefPtr styledElement = dynamicDowncast<StyledElement>(element);
    if (!styledElement)
        return nullptr;

    const auto* properties = styledElement->inlineStyle();
    if (!properties)
        return nullptr;
    return properties->getPropertyCSSValue(propertyId);
}

static bool stringFromCSSValue(CSSValue& value, String& result)
{
    if (auto* primitiveValue = dynamicDowncast<CSSPrimitiveValue>(value)) {
        // FIXME: Use isStringType(CSSUnitType)?
        auto primitiveType = primitiveValue->primitiveType();
        if (primitiveType == CSSUnitType::CSS_STRING || primitiveType == CSSUnitType::CSS_IDENT || primitiveType == CSSUnitType::CSS_ATTR) {
            auto stringValue = value.cssText(CSS::defaultSerializationContext());
            if (stringValue.length()) {
                result = stringValue;
                return true;
            }
        }
    } else if (value.isValueList() || value.isAppleColorFilterPropertyValue() || value.isFilterPropertyValue() || value.isTextShadowPropertyValue() || value.isBoxShadowPropertyValue() || value.isURL()) {
        result = value.cssText(CSS::defaultSerializationContext());
        return true;
    }
    return false;
}

String HTMLConverterCaches::propertyValueForNode(Node& node, CSSPropertyID propertyId)
{
    using namespace ElementNames;

    RefPtr element = dynamicDowncast<Element>(node);
    if (!element) {
        if (RefPtr parent = node.parentInComposedTree())
            return propertyValueForNode(*parent, propertyId);
        return String();
    }

    bool inherit = false;
    if (RefPtr value = computedStylePropertyForElement(*element, propertyId)) {
        String result;
        if (stringFromCSSValue(*value, result))
            return result;
    }

    if (RefPtr value = inlineStylePropertyForElement(*element, propertyId)) {
        String result;
        if (isValueID(*value, CSSValueInherit))
            inherit = true;
        else if (stringFromCSSValue(*value, result))
            return result;
    }

    switch (propertyId) {
    case CSSPropertyDisplay:
        switch (element->elementName()) {
        case HTML::head:
        case HTML::script:
        case HTML::applet:
        case HTML::noframes:
            return noneAtom();
        case HTML::address:
        case HTML::blockquote:
        case HTML::body:
        case HTML::center:
        case HTML::dd:
        case HTML::dir:
        case HTML::div:
        case HTML::dl:
        case HTML::dt:
        case HTML::fieldset:
        case HTML::form:
        case HTML::frame:
        case HTML::frameset:
        case HTML::hr:
        case HTML::html:
        case HTML::h1:
        case HTML::h2:
        case HTML::h3:
        case HTML::h4:
        case HTML::h5:
        case HTML::h6:
        case HTML::iframe:
        case HTML::menu:
        case HTML::noscript:
        case HTML::ol:
        case HTML::p:
        case HTML::pre:
        case HTML::ul:
            return "block"_s;
        case HTML::li:
            return "list-item"_s;
        case HTML::table:
            return "table"_s;
        case HTML::tr:
            return "table-row"_s;
        case HTML::th:
        case HTML::td:
            return "table-cell"_s;
        case HTML::thead:
            return "table-header-group"_s;
        case HTML::tbody:
            return "table-row-group"_s;
        case HTML::tfoot:
            return "table-footer-group"_s;
        case HTML::col:
            return "table-column"_s;
        case HTML::colgroup:
            return "table-column-group"_s;
        case HTML::caption:
            return "table-caption"_s;
        default:
            break;
        }
        break;
    case CSSPropertyWhiteSpace:
        if (element->hasTagName(preTag))
            return "pre"_s;
        inherit = true;
        break;
    case CSSPropertyFontStyle:
        if (element->hasTagName(iTag) || element->hasTagName(citeTag) || element->hasTagName(emTag) || element->hasTagName(varTag) || element->hasTagName(addressTag))
            return "italic"_s;
        inherit = true;
        break;
    case CSSPropertyFontWeight:
        if (element->hasTagName(bTag) || element->hasTagName(strongTag) || element->hasTagName(thTag))
            return "bolder"_s;
        inherit = true;
        break;
    case CSSPropertyTextDecorationLine:
        switch (element->elementName()) {
        case HTML::u:
        case HTML::ins:
            return "underline"_s;
        case HTML::s:
        case HTML::strike:
        case HTML::del:
            return "line-through"_s;
        default:
            break;
        }
        inherit = true; // FIXME: This is not strictly correct
        break;
    case CSSPropertyTextAlign:
        if (element->hasTagName(centerTag) || element->hasTagName(captionTag) || element->hasTagName(thTag))
            return "center"_s;
        inherit = true;
        break;
    case CSSPropertyVerticalAlign:
        switch (element->elementName()) {
        case HTML::sup:
            return "super"_s;
        case HTML::sub:
            return "sub"_s;
        case HTML::thead:
        case HTML::tbody:
        case HTML::tfoot:
            return "middle"_s;
        case HTML::tr:
        case HTML::th:
        case HTML::td:
            inherit = true;
            break;
        default:
            break;
        }
        break;
    case CSSPropertyFontFamily:
    case CSSPropertyFontVariantCaps:
    case CSSPropertyTextTransform:
    case CSSPropertyTextShadow:
    case CSSPropertyVisibility:
    case CSSPropertyBorderCollapse:
    case CSSPropertyEmptyCells:
    case CSSPropertyWordSpacing:
    case CSSPropertyListStyleType:
    case CSSPropertyDirection:
        inherit = true; // FIXME: Let classes in the css component figure this out.
        break;
    default:
        break;
    }

    if (inherit) {
        if (RefPtr parent = node.parentInComposedTree())
            return propertyValueForNode(*parent, propertyId);
    }

    return String();
}

static inline bool floatValueFromPrimitiveValue(CSSPrimitiveValue& primitiveValue, float& result)
{
    switch (primitiveValue.primitiveType()) {
    case CSSUnitType::CSS_PX:
    case CSSUnitType::CSS_PT:
    case CSSUnitType::CSS_PC:
    case CSSUnitType::CSS_CM:
    case CSSUnitType::CSS_MM:
    case CSSUnitType::CSS_Q:
    case CSSUnitType::CSS_IN:
        result = primitiveValue.resolveAsLengthDeprecated();
        return true;
    default:
        return false;
    }
}

bool HTMLConverterCaches::floatPropertyValueForNode(Node& node, CSSPropertyID propertyId, float& result)
{
    RefPtr element = dynamicDowncast<Element>(node);
    if (!element) {
        if (RefPtr parent = node.parentInComposedTree())
            return floatPropertyValueForNode(*parent, propertyId, result);
        return false;
    }

    if (RefPtr value = computedStylePropertyForElement(*element, propertyId)) {
        if (RefPtr primitiveValue = dynamicDowncast<CSSPrimitiveValue>(*value); primitiveValue && floatValueFromPrimitiveValue(*primitiveValue, result))
            return true;
    }

    bool inherit = false;
    if (RefPtr value = inlineStylePropertyForElement(*element, propertyId)) {
        if (RefPtr primitiveValue = dynamicDowncast<CSSPrimitiveValue>(*value); primitiveValue && floatValueFromPrimitiveValue(*primitiveValue, result))
            return true;
        if (isValueID(*value, CSSValueInherit))
            inherit = true;
    }

    switch (propertyId) {
    case CSSPropertyTextIndent:
    case CSSPropertyLetterSpacing:
    case CSSPropertyWordSpacing:
    case CSSPropertyLineHeight:
    case CSSPropertyWidows:
    case CSSPropertyOrphans:
        inherit = true;
        break;
    default:
        break;
    }

    if (inherit) {
        if (RefPtr parent = node.parentInComposedTree())
            return floatPropertyValueForNode(*parent, propertyId, result);
    }

    return false;
}

static inline NSShadow *_shadowForShadowStyle(NSString *shadowStyle)
{
    RetainPtr<NSShadow> shadow;
    NSUInteger shadowStyleLength = [shadowStyle length];
    NSRange openParenRange = [shadowStyle rangeOfString:@"("];
    NSRange closeParenRange = [shadowStyle rangeOfString:@")"];
    NSRange firstRange = NSMakeRange(NSNotFound, 0);
    NSRange secondRange = NSMakeRange(NSNotFound, 0);
    NSRange thirdRange = NSMakeRange(NSNotFound, 0);
    NSRange spaceRange;
    if (openParenRange.length > 0 && closeParenRange.length > 0 && NSMaxRange(openParenRange) < closeParenRange.location) {
        NSArray *components = [[shadowStyle substringWithRange:NSMakeRange(NSMaxRange(openParenRange), closeParenRange.location - NSMaxRange(openParenRange))] componentsSeparatedByString:@","];
        if ([components count] >= 3) {
            CGFloat red = [[components objectAtIndex:0] floatValue] / 255;
            CGFloat green = [[components objectAtIndex:1] floatValue] / 255;
            CGFloat blue = [[components objectAtIndex:2] floatValue] / 255;
            CGFloat alpha = ([components count] >= 4) ? [[components objectAtIndex:3] floatValue] / 255 : 1;
            NSColor *shadowColor = [PlatformNSColorClass colorWithCalibratedRed:red green:green blue:blue alpha:alpha];
            NSSize shadowOffset;
            CGFloat shadowBlurRadius;
            firstRange = [shadowStyle rangeOfString:@"px"];
            if (firstRange.length > 0 && NSMaxRange(firstRange) < shadowStyleLength)
                secondRange = [shadowStyle rangeOfString:@"px" options:0 range:NSMakeRange(NSMaxRange(firstRange), shadowStyleLength - NSMaxRange(firstRange))];
            if (secondRange.length > 0 && NSMaxRange(secondRange) < shadowStyleLength)
                thirdRange = [shadowStyle rangeOfString:@"px" options:0 range:NSMakeRange(NSMaxRange(secondRange), shadowStyleLength - NSMaxRange(secondRange))];
            if (firstRange.location > 0 && firstRange.length > 0 && secondRange.length > 0 && thirdRange.length > 0) {
                spaceRange = [shadowStyle rangeOfString:@" " options:NSBackwardsSearch range:NSMakeRange(0, firstRange.location)];
                if (spaceRange.length == 0)
                    spaceRange = NSMakeRange(0, 0);
                shadowOffset.width = [[shadowStyle substringWithRange:NSMakeRange(NSMaxRange(spaceRange), firstRange.location - NSMaxRange(spaceRange))] floatValue];
                spaceRange = [shadowStyle rangeOfString:@" " options:NSBackwardsSearch range:NSMakeRange(0, secondRange.location)];
                if (!spaceRange.length)
                    spaceRange = NSMakeRange(0, 0);
                CGFloat shadowHeight = [[shadowStyle substringWithRange:NSMakeRange(NSMaxRange(spaceRange), secondRange.location - NSMaxRange(spaceRange))] floatValue];
                // I don't know why we have this difference between the two platforms.
#if PLATFORM(IOS_FAMILY)
                shadowOffset.height = shadowHeight;
#else
                shadowOffset.height = -shadowHeight;
#endif
                spaceRange = [shadowStyle rangeOfString:@" " options:NSBackwardsSearch range:NSMakeRange(0, thirdRange.location)];
                if (!spaceRange.length)
                    spaceRange = NSMakeRange(0, 0);
                shadowBlurRadius = [[shadowStyle substringWithRange:NSMakeRange(NSMaxRange(spaceRange), thirdRange.location - NSMaxRange(spaceRange))] floatValue];
                shadow = adoptNS([(NSShadow *)[PlatformNSShadow alloc] init]);
                [shadow setShadowColor:shadowColor];
                [shadow setShadowOffset:shadowOffset];
                [shadow setShadowBlurRadius:shadowBlurRadius];
            }
        }
    }
    return shadow.autorelease();
}

bool HTMLConverterCaches::isBlockElement(Element& element)
{
    String displayValue = propertyValueForNode(element, CSSPropertyDisplay);
    if (displayValue == "block"_s || displayValue == "list-item"_s || displayValue.startsWith("table"_s))
        return true;
    String floatValue = propertyValueForNode(element, CSSPropertyFloat);
    if (floatValue == "left"_s || floatValue == "right"_s)
        return true;
    return false;
}

bool HTMLConverterCaches::elementHasOwnBackgroundColor(Element& element)
{
    if (!isBlockElement(element))
        return false;
    // In the text system, text blocks (table elements) and documents (body elements)
    // have their own background colors, which should not be inherited.
    return element.hasTagName(htmlTag) || element.hasTagName(bodyTag) || propertyValueForNode(element, CSSPropertyDisplay).startsWith("table"_s);
}

Element* HTMLConverter::_blockLevelElementForNode(Node* node)
{
    RefPtr element = dynamicDowncast<Element>(node);
    if (!element)
        element = node->parentElement();
    if (element && !_caches->isBlockElement(*element))
        element = _blockLevelElementForNode(element->parentInComposedTree());
    return element.get();
}

static Color normalizedColor(Color color, bool ignoreDefaultColor, Element& element)
{
    if (!ignoreDefaultColor)
        return color;

    bool useDarkAppearance = element.document().useDarkAppearance(element.existingComputedStyle());
    if (useDarkAppearance && Color::isWhiteColor(color))
        return Color();

    if (!useDarkAppearance && Color::isBlackColor(color))
        return Color();

    return color;
}

Color HTMLConverterCaches::colorPropertyValueForNode(Node& node, CSSPropertyID propertyId)
{
    RefPtr element = dynamicDowncast<Element>(node);
    if (!element) {
        if (RefPtr parent = node.parentInComposedTree())
            return colorPropertyValueForNode(*parent, propertyId);
        return Color();
    }

    bool ignoreDefaultColor = propertyId == CSSPropertyColor;

    if (auto value = computedStylePropertyForElement(*element, propertyId); value && value->isColor())
        return normalizedColor(CSSColorValue::absoluteColor(*value), ignoreDefaultColor, *element);

    bool inherit = false;
    if (auto value = inlineStylePropertyForElement(*element, propertyId)) {
        if (value->isColor())
            return normalizedColor(CSSColorValue::absoluteColor(*value), ignoreDefaultColor, *element);
        if (isValueID(*value, CSSValueInherit))
            inherit = true;
    }

    switch (propertyId) {
    case CSSPropertyColor:
        inherit = true;
        break;
    case CSSPropertyBackgroundColor:
        if (!elementHasOwnBackgroundColor(*element)) {
            if (auto* parentElement = node.parentElement()) {
                if (!elementHasOwnBackgroundColor(*parentElement))
                    inherit = true;
            }
        }
        break;
    default:
        break;
    }

    if (inherit) {
        if (RefPtr parent = node.parentInComposedTree())
            return colorPropertyValueForNode(*parent, propertyId);
    }

    return Color();
}

RetainPtr<PlatformColor> HTMLConverter::_colorForElement(Element& element, CSSPropertyID propertyId)
{
    Color result = _caches->colorPropertyValueForNode(element, propertyId);
    if (!result.isValid())
        return nil;
    auto platformResult = cocoaColor(result);
    if ([[PlatformColorClass clearColor] isEqual:platformResult.get()] || ([platformResult alphaComponent] == 0.0))
        return nil;
    return platformResult;
}

static PlatformFont *_font(Element& element)
{
    auto* renderer = element.renderer();
    if (!renderer)
        return nil;
    Ref primaryFont = renderer->style().fontCascade().primaryFont();
    if (primaryFont->attributes().origin == FontOrigin::Remote)
        return [PlatformFontClass systemFontOfSize:defaultFontSize];
    return (__bridge PlatformFont *)primaryFont->getCTFont();
}

NSDictionary *HTMLConverter::computedAttributesForElement(Element& element)
{
    NSMutableDictionary *attrs = [NSMutableDictionary dictionary];
#if !PLATFORM(IOS_FAMILY)
    NSFontManager *fontManager = [NSFontManager sharedFontManager];
#endif

    PlatformFont *font = nil;
    PlatformFont *actualFont = _font(element);
    auto foregroundColor = _colorForElement(element, CSSPropertyColor);
    auto backgroundColor = _colorForElement(element, CSSPropertyBackgroundColor);
    auto strokeColor = _colorForElement(element, CSSPropertyWebkitTextStrokeColor);

    float fontSize = 0;
    if (!_caches->floatPropertyValueForNode(element, CSSPropertyFontSize, fontSize) || fontSize <= 0.0)
        fontSize = defaultFontSize;
    if (fontSize < minimumFontSize)
        fontSize = minimumFontSize;
    if (std::abs(floor(2.0 * fontSize + 0.5) / 2.0 - fontSize) < 0.05)
        fontSize = floor(2.0 * fontSize + 0.5) / 2;
    else if (std::abs(floor(10.0 * fontSize + 0.5) / 10.0 - fontSize) < 0.005)
        fontSize = floor(10.0 * fontSize + 0.5) / 10;

    if (fontSize <= 0.0)
        fontSize = defaultFontSize;

#if PLATFORM(IOS_FAMILY)
    if (actualFont)
        font = [actualFont fontWithSize:fontSize];
#else
    if (actualFont)
        font = [fontManager convertFont:actualFont toSize:fontSize];
#endif
    if (!font) {
        String fontName = _caches->propertyValueForNode(element, CSSPropertyFontFamily);
        if (fontName.length())
            font = _fontForNameAndSize(fontName.convertToASCIILowercase().createNSString().get(), fontSize, _fontCache.get());
        if (!font)
            font = [PlatformFontClass fontWithName:@"Times" size:fontSize];

        String fontStyle = _caches->propertyValueForNode(element, CSSPropertyFontStyle);
        if (fontStyle == "italic"_s || fontStyle == "oblique"_s) {
            PlatformFont *originalFont = font;
#if PLATFORM(IOS_FAMILY)
            font = [PlatformFontClass fontWithFamilyName:[font familyName] traits:UIFontTraitItalic size:[font pointSize]];
#else
            font = [fontManager convertFont:font toHaveTrait:NSItalicFontMask];
#endif
            if (!font)
                font = originalFont;
        }

        String fontWeight = _caches->propertyValueForNode(element, CSSPropertyFontStyle);
        if (fontWeight.startsWith("bold"_s) || parseIntegerAllowingTrailingJunk<int>(fontWeight).value_or(0) >= 700) {
            // ??? handle weight properly using NSFontManager
            PlatformFont *originalFont = font;
#if PLATFORM(IOS_FAMILY)
            font = [PlatformFontClass fontWithFamilyName:[font familyName] traits:UIFontTraitBold size:[font pointSize]];
#else
            font = [fontManager convertFont:font toHaveTrait:NSBoldFontMask];
#endif
            if (!font)
                font = originalFont;
        }
#if !PLATFORM(IOS_FAMILY) // IJB: No small caps support on iOS
        if (_caches->propertyValueForNode(element, CSSPropertyFontVariantCaps) == "small-caps"_s) {
            // ??? synthesize small-caps if [font isEqual:originalFont]
            NSFont *originalFont = font;
            font = [fontManager convertFont:font toHaveTrait:NSSmallCapsFontMask];
            if (!font)
                font = originalFont;
        }
#endif
    }
    if (font)
        [attrs setObject:font forKey:NSFontAttributeName];
    if (foregroundColor)
        [attrs setObject:foregroundColor.get() forKey:NSForegroundColorAttributeName];
    if (backgroundColor && !_caches->elementHasOwnBackgroundColor(element))
        [attrs setObject:backgroundColor.get() forKey:NSBackgroundColorAttributeName];

    float strokeWidth = 0.0;
    if (_caches->floatPropertyValueForNode(element, CSSPropertyWebkitTextStrokeWidth, strokeWidth)) {
        float textStrokeWidth = strokeWidth / ([font pointSize] * 0.01);
        [attrs setObject:@(textStrokeWidth) forKey:NSStrokeWidthAttributeName];
    }
    if (strokeColor)
        [attrs setObject:strokeColor.get() forKey:NSStrokeColorAttributeName];

    String fontKerning = _caches->propertyValueForNode(element, CSSPropertyFontKerning);
    String letterSpacing = _caches->propertyValueForNode(element, CSSPropertyLetterSpacing);
    if (fontKerning.length() || letterSpacing.length()) {
        if (fontKerning == noneAtom())
            [attrs setObject:@0.0 forKey:NSKernAttributeName];
        else {
            double kernVal = letterSpacing.length() ? letterSpacing.toDouble() : 0.0;
            if (std::abs(kernVal - 0) < FLT_EPSILON)
                [attrs setObject:@0.0 forKey:NSKernAttributeName]; // auto and normal, the other possible values, are both "kerning enabled"
            else
                [attrs setObject:@(kernVal) forKey:NSKernAttributeName];
        }
    }

    String fontLigatures = _caches->propertyValueForNode(element, CSSPropertyFontVariantLigatures);
    if (fontLigatures.length()) {
        if (fontLigatures.contains("normal"_s))
            ;   // default: whatever the system decides to do
        else if (fontLigatures.contains("common-ligatures"_s))
            [attrs setObject:@1 forKey:NSLigatureAttributeName];   // explicitly enabled
        else if (fontLigatures.contains("no-common-ligatures"_s))
            [attrs setObject:@0 forKey:NSLigatureAttributeName];  // explicitly disabled
    }

    String textDecoration = _caches->propertyValueForNode(element, CSSPropertyTextDecorationLine);
    if (textDecoration.length()) {
        if (textDecoration.contains("underline"_s))
            [attrs setObject:[NSNumber numberWithInteger:NSUnderlineStyleSingle] forKey:NSUnderlineStyleAttributeName];
        if (textDecoration.contains("line-through"_s))
            [attrs setObject:[NSNumber numberWithInteger:NSUnderlineStyleSingle] forKey:NSStrikethroughStyleAttributeName];
    }

    String verticalAlign = _caches->propertyValueForNode(element, CSSPropertyVerticalAlign);
    if (verticalAlign.length()) {
        if (verticalAlign == "super"_s)
            [attrs setObject:[NSNumber numberWithInteger:1] forKey:NSSuperscriptAttributeName];
        else if (verticalAlign == "sub"_s)
            [attrs setObject:[NSNumber numberWithInteger:-1] forKey:NSSuperscriptAttributeName];
    }

    float baselineOffset = 0.0;
    if (_caches->floatPropertyValueForNode(element, CSSPropertyVerticalAlign, baselineOffset))
        [attrs setObject:@(baselineOffset) forKey:NSBaselineOffsetAttributeName];

    String textShadow = _caches->propertyValueForNode(element, CSSPropertyTextShadow);
    if (textShadow.length() > 4) {
        NSShadow *shadow = _shadowForShadowStyle(textShadow.createNSString().get());
        if (shadow)
            [attrs setObject:shadow forKey:NSShadowAttributeName];
    }

    Element* blockElement = _blockLevelElementForNode(&element);
    if (&element != blockElement && [_writingDirectionArray count] > 0)
        [attrs setObject:[NSArray arrayWithArray:_writingDirectionArray.get()] forKey:NSWritingDirectionAttributeName];

    if (blockElement) {
        Element& coreBlockElement = *blockElement;
        RetainPtr<NSMutableParagraphStyle> paragraphStyle = adoptNS([defaultParagraphStyle() mutableCopy]);
        unsigned heading = 0;
        if (coreBlockElement.hasTagName(h1Tag))
            heading = 1;
        else if (coreBlockElement.hasTagName(h2Tag))
            heading = 2;
        else if (coreBlockElement.hasTagName(h3Tag))
            heading = 3;
        else if (coreBlockElement.hasTagName(h4Tag))
            heading = 4;
        else if (coreBlockElement.hasTagName(h5Tag))
            heading = 5;
        else if (coreBlockElement.hasTagName(h6Tag))
            heading = 6;
        else if (coreBlockElement.hasTagName(blockquoteTag) && _topPresentationIntent)
            [attrs setObject:_topPresentationIntent.get() forKey:NSPresentationIntentAttributeName];
        bool isParagraph = coreBlockElement.hasTagName(pTag) || coreBlockElement.hasTagName(liTag) || heading || coreBlockElement.hasTagName(blockquoteTag);

        String textAlign = _caches->propertyValueForNode(coreBlockElement, CSSPropertyTextAlign);
        if (textAlign.length()) {
            // WebKit can return -khtml-left, -khtml-right, -khtml-center
            if (textAlign.endsWith("left"_s))
                [paragraphStyle setAlignment:NSTextAlignmentLeft];
            else if (textAlign.endsWith("right"_s))
                [paragraphStyle setAlignment:NSTextAlignmentRight];
            else if (textAlign.endsWith("center"_s))
                [paragraphStyle setAlignment:NSTextAlignmentCenter];
            else if (textAlign.endsWith("justify"_s))
                [paragraphStyle setAlignment:NSTextAlignmentJustified];
        }

        String direction = _caches->propertyValueForNode(coreBlockElement, CSSPropertyDirection);
        if (direction.length()) {
            if (direction == "ltr"_s)
                [paragraphStyle setBaseWritingDirection:NSWritingDirectionLeftToRight];
            else if (direction == "rtl"_s)
                [paragraphStyle setBaseWritingDirection:NSWritingDirectionRightToLeft];
        }

        String hyphenation = _caches->propertyValueForNode(coreBlockElement, CSSPropertyHyphens);
        if (hyphenation.length()) {
            if (hyphenation == autoAtom())
                [paragraphStyle setHyphenationFactor:1.0];
            else
                [paragraphStyle setHyphenationFactor:0.0];
        }
        if (heading)
            [paragraphStyle setHeaderLevel:heading];
        if (isParagraph) {
            // FIXME: Why are we ignoring margin-top?
            float marginLeft = 0.0;
            if (_caches->floatPropertyValueForNode(coreBlockElement, CSSPropertyMarginLeft, marginLeft) && marginLeft > 0.0)
                [paragraphStyle setHeadIndent:marginLeft];
            float textIndent = 0.0;
            if (_caches->floatPropertyValueForNode(coreBlockElement, CSSPropertyTextIndent, textIndent) && textIndent > 0.0)
                [paragraphStyle setFirstLineHeadIndent:[paragraphStyle headIndent] + textIndent];
            float marginRight = 0.0;
            if (_caches->floatPropertyValueForNode(coreBlockElement, CSSPropertyMarginRight, marginRight) && marginRight > 0.0)
                [paragraphStyle setTailIndent:-marginRight];
            float marginBottom = 0.0;
            if (_caches->floatPropertyValueForNode(coreBlockElement, CSSPropertyMarginBottom, marginBottom) && marginBottom > 0.0)
                [paragraphStyle setParagraphSpacing:marginBottom];
        }
        if ([_textLists count] > 0)
            [paragraphStyle setTextLists:_textLists.get()];
        if ([_textBlocks count] > 0)
            [paragraphStyle setTextBlocks:_textBlocks.get()];
        [attrs setObject:paragraphStyle.get() forKey:NSParagraphStyleAttributeName];
    }
    return attrs;
}


NSDictionary* HTMLConverter::attributesForElement(Element& element)
{
    auto& attributes = m_attributesForElements.add(&element, nullptr).iterator->value;
    if (!attributes)
        attributes = computedAttributesForElement(element);
    return attributes.get();
}

NSDictionary* HTMLConverter::aggregatedAttributesForAncestors(CharacterData& node)
{
    Node* ancestor = node.parentInComposedTree();
    while (ancestor && !is<Element>(*ancestor))
        ancestor = ancestor->parentInComposedTree();
    if (!ancestor)
        return nullptr;
    return aggregatedAttributesForElementAndItsAncestors(downcast<Element>(*ancestor));
}

NSDictionary* HTMLConverter::aggregatedAttributesForElementAndItsAncestors(Element& element)
{
    auto& cachedAttributes = m_aggregatedAttributesForElements.add(&element, nullptr).iterator->value;
    if (cachedAttributes)
        return cachedAttributes.get();

    NSDictionary* attributesForCurrentElement = attributesForElement(element);
    ASSERT(attributesForCurrentElement);

    Node* ancestor = element.parentInComposedTree();
    while (ancestor && !is<Element>(*ancestor))
        ancestor = ancestor->parentInComposedTree();

    if (!ancestor) {
        cachedAttributes = attributesForCurrentElement;
        return attributesForCurrentElement;
    }

    RetainPtr<NSMutableDictionary> attributesForAncestors = adoptNS([aggregatedAttributesForElementAndItsAncestors(downcast<Element>(*ancestor)) mutableCopy]);
    [attributesForAncestors addEntriesFromDictionary:attributesForCurrentElement];
    m_aggregatedAttributesForElements.set(&element, attributesForAncestors);

    return attributesForAncestors.get();
}

void HTMLConverter::_newParagraphForElement(Element& element, NSString *tag, BOOL flag, BOOL suppressTrailingSpace)
{
    NSUInteger textLength = [_attrStr length];
    unichar lastChar = (textLength > 0) ? [[_attrStr string] characterAtIndex:textLength - 1] : '\n';
    NSRange rangeToReplace = (suppressTrailingSpace && _flags.isSoft && (lastChar == ' ' || lastChar == NSLineSeparatorCharacter)) ? NSMakeRange(textLength - 1, 1) : NSMakeRange(textLength, 0);
    BOOL needBreak = (flag || lastChar != '\n');
    if (needBreak) {
        NSString *string = (([@"BODY" isEqualToString:tag] || [@"HTML" isEqualToString:tag]) ? @"" : @"\n");
        [_writingDirectionArray removeAllObjects];
        [_attrStr replaceCharactersInRange:rangeToReplace withString:string];
        if (rangeToReplace.location < _domRangeStartIndex)
            _domRangeStartIndex += [string length] - rangeToReplace.length;
        rangeToReplace.length = [string length];
        NSDictionary *attrs = attributesForElement(element);
        if (rangeToReplace.length > 0)
            [_attrStr setAttributes:attrs range:rangeToReplace];
        _flags.isSoft = YES;
    }
}

void HTMLConverter::_newLineForElement(Element& element)
{
    unichar c = NSLineSeparatorCharacter;
    RetainPtr<NSString> string = adoptNS([[NSString alloc] initWithCharacters:&c length:1]);
    NSUInteger textLength = [_attrStr length];
    NSRange rangeToReplace = NSMakeRange(textLength, 0);
    [_attrStr replaceCharactersInRange:rangeToReplace withString:string.get()];
    rangeToReplace.length = [string length];
    if (rangeToReplace.location < _domRangeStartIndex)
        _domRangeStartIndex += rangeToReplace.length;
    NSDictionary *attrs = attributesForElement(element);
    if (rangeToReplace.length > 0)
        [_attrStr setAttributes:attrs range:rangeToReplace];
    _flags.isSoft = YES;
}

void HTMLConverter::_newTabForElement(Element& element)
{
    NSString *string = @"\t";
    NSUInteger textLength = [_attrStr length];
    unichar lastChar = (textLength > 0) ? [[_attrStr string] characterAtIndex:textLength - 1] : '\n';
    NSRange rangeToReplace = (_flags.isSoft && lastChar == ' ') ? NSMakeRange(textLength - 1, 1) : NSMakeRange(textLength, 0);
    [_attrStr replaceCharactersInRange:rangeToReplace withString:string];
    rangeToReplace.length = [string length];
    if (rangeToReplace.location < _domRangeStartIndex)
        _domRangeStartIndex += rangeToReplace.length;
    NSDictionary *attrs = attributesForElement(element);
    if (rangeToReplace.length > 0)
        [_attrStr setAttributes:attrs range:rangeToReplace];
    _flags.isSoft = YES;
}

static Class _WebMessageDocumentClass()
{
    static Class _WebMessageDocumentClass = Nil;
    static BOOL lookedUpClass = NO;
    if (!lookedUpClass) {
        // If the class is not there, we don't want to try again
#if PLATFORM(MAC)
        _WebMessageDocumentClass = objc_lookUpClass("EditableWebMessageDocument");
#endif
        if (!_WebMessageDocumentClass)
            _WebMessageDocumentClass = objc_lookUpClass("WebMessageDocument");

        if (_WebMessageDocumentClass && ![_WebMessageDocumentClass respondsToSelector:@selector(document:attachment:forURL:)])
            _WebMessageDocumentClass = Nil;
        lookedUpClass = YES;
    }
    return _WebMessageDocumentClass;
}

#if ENABLE(MULTI_REPRESENTATION_HEIC)
BOOL HTMLConverter::_addMultiRepresentationHEICAttachmentForImageElement(HTMLImageElement& element)
{
    RefPtr image = element.image();
    if (!image)
        return NO;

    NSAdaptiveImageGlyph *attachment = image->adapter().multiRepresentationHEIC();
    if (!attachment)
        return NO;

    NSUInteger textLength = [_attrStr length];

    RetainPtr string = adoptNS([[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSAttachmentCharacter)]);
    NSRange rangeToReplace = NSMakeRange(textLength, 0);

    [_attrStr replaceCharactersInRange:rangeToReplace withString:string.get()];
    rangeToReplace.length = [string length];
    if (rangeToReplace.location < _domRangeStartIndex)
        _domRangeStartIndex += rangeToReplace.length;

    [_attrStr addAttribute:NSAdaptiveImageGlyphAttributeName value:attachment range:rangeToReplace];

    _flags.isSoft = NO;
    return YES;
}
#endif // ENABLE(MULTI_REPRESENTATION_HEIC)

BOOL HTMLConverter::_addAttachmentForElement(Element& element, NSURL *url, BOOL needsParagraph, BOOL usePlaceholder)
{
    BOOL retval = NO;
    BOOL notFound = NO;
    RetainPtr<NSFileWrapper> fileWrapper;
    auto* frame = element.document().frame();
    DocumentLoader *dataSource = frame->loader().frameHasLoaded() ? frame->loader().documentLoader() : 0;
    BOOL ignoreOrientation = YES;

    if ([url isFileURL]) {
        NSString *path = [[url path] stringByStandardizingPath];
        if (path)
            fileWrapper = adoptNS([[NSFileWrapper alloc] initWithURL:url options:0 error:NULL]);
    }
    if (!fileWrapper && dataSource) {
        if (auto resource = dataSource->subresource(url)) {
            auto& mimeType = resource->mimeType();
            if (!usePlaceholder || mimeType != textHTMLContentTypeAtom()) {
                fileWrapper = adoptNS([[NSFileWrapper alloc] initRegularFileWithContents:resource->data().makeContiguous()->createNSData().get()]);
                [fileWrapper setPreferredFilename:suggestedFilenameWithMIMEType(url, mimeType)];
            } else
                notFound = YES;
        }
    }
#if !PLATFORM(IOS_FAMILY)
    if (!fileWrapper && !notFound) {
        fileWrapper = fileWrapperForURL(dataSource, url);
        if (usePlaceholder && fileWrapper && [[[[fileWrapper preferredFilename] pathExtension] lowercaseString] hasPrefix:@"htm"])
            notFound = YES;
        if (notFound)
            fileWrapper = nil;
    }
    if (!fileWrapper && !notFound) {
        fileWrapper = fileWrapperForURL(m_dataSource, url);
        if (usePlaceholder && fileWrapper && [[[[fileWrapper preferredFilename] pathExtension] lowercaseString] hasPrefix:@"htm"])
            notFound = YES;
        if (notFound)
            fileWrapper = nil;
    }
#endif
    if (!fileWrapper && !notFound && url) {
        // Special handling for Mail attachments, until WebKit provides a standard way to get the data.
        Class WebMessageDocumentClass = _WebMessageDocumentClass();
        if (WebMessageDocumentClass) {
            NSTextAttachment *mimeTextAttachment = nil;
            [WebMessageDocumentClass document:NULL attachment:&mimeTextAttachment forURL:url];
            if (mimeTextAttachment && [mimeTextAttachment respondsToSelector:@selector(fileWrapper)]) {
                fileWrapper = [mimeTextAttachment fileWrapper];
                ignoreOrientation = NO;
            }
        }
    }
    if (fileWrapper || usePlaceholder) {
        RetainPtr<id> attachment;
        NSAttributedStringKey attributeName = NSAttachmentAttributeName;

        NSUInteger textLength = [_attrStr length];
        RetainPtr string = adoptNS([[NSString alloc] initWithFormat:(needsParagraph ? @"%C\n" : @"%C"), static_cast<unichar>(NSAttachmentCharacter)]);
        NSRange rangeToReplace = NSMakeRange(textLength, 0);
        NSDictionary *attrs;

#if ENABLE(MULTI_REPRESENTATION_HEIC)
        if (RetainPtr data = [fileWrapper regularFileContents]) {
            RefPtr imageElement = dynamicDowncast<HTMLImageElement>(element);
            if (imageElement && imageElement->isMultiRepresentationHEIC())
                attachment = adoptNS([[PlatformNSAdaptiveImageGlyph alloc] initWithImageContent:data.get()]);
            if (attachment)
                attributeName = NSAdaptiveImageGlyphAttributeName;
        }
#endif

        if (!attachment) {
            RetainPtr textAttachment = adoptNS([[PlatformNSTextAttachment alloc] initWithFileWrapper:fileWrapper.get()]);

            if (auto& ariaLabel = element.getAttribute("aria-label"_s); !ariaLabel.isEmpty())
                [textAttachment setAccessibilityLabel:ariaLabel.createNSString().get()];
            if (auto& altText = element.getAttribute("alt"_s); !altText.isEmpty())
                [textAttachment setAccessibilityLabel:altText.createNSString().get()];

#if PLATFORM(IOS_FAMILY)
            float verticalAlign = 0.0;
            _caches->floatPropertyValueForNode(element, CSSPropertyVerticalAlign, verticalAlign);
            [textAttachment setBounds:CGRectMake(0, (verticalAlign / 100) * element.clientHeight(), element.clientWidth(), element.clientHeight())];
#endif
            if (fileWrapper) {
#if PLATFORM(IOS_FAMILY)
                UNUSED_VARIABLE(ignoreOrientation);
#else
                if (ignoreOrientation)
                    [textAttachment setIgnoresOrientation:YES];
#endif
            } else {
                textAttachment = adoptNS([[PlatformNSTextAttachment alloc] initWithData:nil ofType:nil]);
                [textAttachment setImage:webCoreTextAttachmentMissingPlatformImage()];
            }

            attachment = textAttachment;
        }

        [_attrStr replaceCharactersInRange:rangeToReplace withString:string.get()];
        rangeToReplace.length = [string length];
        if (rangeToReplace.location < _domRangeStartIndex)
            _domRangeStartIndex += rangeToReplace.length;
        attrs = attributesForElement(element);
        if (rangeToReplace.length > 0) {
            [_attrStr setAttributes:attrs range:rangeToReplace];
            rangeToReplace.length = 1;
            [_attrStr addAttribute:attributeName value:attachment.get() range:rangeToReplace];
        }
        _flags.isSoft = NO;
        retval = YES;
    }
    return retval;
}

void HTMLConverter::_addQuoteForElement(Element& element, BOOL opening, NSInteger level)
{
    unichar c = ((level % 2) == 0) ? (opening ? 0x201c : 0x201d) : (opening ? 0x2018 : 0x2019);
    RetainPtr<NSString> string = adoptNS([[NSString alloc] initWithCharacters:&c length:1]);
    NSUInteger textLength = [_attrStr length];
    NSRange rangeToReplace = NSMakeRange(textLength, 0);
    [_attrStr replaceCharactersInRange:rangeToReplace withString:string.get()];
    rangeToReplace.length = [string length];
    if (rangeToReplace.location < _domRangeStartIndex)
        _domRangeStartIndex += rangeToReplace.length;
    RetainPtr<NSDictionary> attrs = attributesForElement(element);
    if (rangeToReplace.length > 0)
        [_attrStr setAttributes:attrs.get() range:rangeToReplace];
    _flags.isSoft = NO;
}

void HTMLConverter::_addValue(NSString *value, Element& element)
{
    NSUInteger textLength = [_attrStr length];
    NSUInteger valueLength = [value length];
    NSRange rangeToReplace = NSMakeRange(textLength, 0);
    if (valueLength) {
        [_attrStr replaceCharactersInRange:rangeToReplace withString:value];
        rangeToReplace.length = valueLength;
        if (rangeToReplace.location < _domRangeStartIndex)
            _domRangeStartIndex += rangeToReplace.length;
        RetainPtr<NSDictionary> attrs = attributesForElement(element);
        if (rangeToReplace.length > 0)
            [_attrStr setAttributes:attrs.get() range:rangeToReplace];
        _flags.isSoft = NO;
    }
}

void HTMLConverter::_fillInBlock(NSTextBlock *block, Element& element, PlatformColor *backgroundColor, CGFloat extraMargin, CGFloat extraPadding, BOOL isTable)
{
    float result = 0;

    RetainPtr width = element.getAttribute(widthAttr).createNSString();
    if ((width && [width length]) || !isTable) {
        if (_caches->floatPropertyValueForNode(element, CSSPropertyWidth, result))
            [block setValue:result type:NSTextBlockAbsoluteValueType forDimension:NSTextBlockWidth];
    }

    if (_caches->floatPropertyValueForNode(element, CSSPropertyMinWidth, result))
        [block setValue:result type:NSTextBlockAbsoluteValueType forDimension:NSTextBlockMinimumWidth];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyMaxWidth, result))
        [block setValue:result type:NSTextBlockAbsoluteValueType forDimension:NSTextBlockMaximumWidth];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyMinHeight, result))
        [block setValue:result type:NSTextBlockAbsoluteValueType forDimension:NSTextBlockMinimumHeight];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyMaxHeight, result))
        [block setValue:result type:NSTextBlockAbsoluteValueType forDimension:NSTextBlockMaximumHeight];

    if (_caches->floatPropertyValueForNode(element, CSSPropertyPaddingLeft, result))
        [block setWidth:result + extraPadding type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding edge:NSMinXEdge];
    else
        [block setWidth:extraPadding type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding edge:NSMinXEdge];

    if (_caches->floatPropertyValueForNode(element, CSSPropertyPaddingTop, result))
        [block setWidth:result + extraPadding type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding edge:NSMinYEdge];
    else
        [block setWidth:extraPadding type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding edge:NSMinYEdge];

    if (_caches->floatPropertyValueForNode(element, CSSPropertyPaddingRight, result))
        [block setWidth:result + extraPadding type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding edge:NSMaxXEdge];
    else
        [block setWidth:extraPadding type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding edge:NSMaxXEdge];

    if (_caches->floatPropertyValueForNode(element, CSSPropertyPaddingBottom, result))
        [block setWidth:result + extraPadding type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding edge:NSMaxYEdge];
    else
        [block setWidth:extraPadding type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding edge:NSMaxYEdge];

    if (_caches->floatPropertyValueForNode(element, CSSPropertyBorderLeftWidth, result))
        [block setWidth:result type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockBorder edge:NSMinXEdge];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyBorderTopWidth, result))
        [block setWidth:result type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockBorder edge:NSMinYEdge];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyBorderRightWidth, result))
        [block setWidth:result type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockBorder edge:NSMaxXEdge];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyBorderBottomWidth, result))
        [block setWidth:result type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockBorder edge:NSMaxYEdge];

    if (_caches->floatPropertyValueForNode(element, CSSPropertyMarginLeft, result))
        [block setWidth:result + extraMargin type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockMargin edge:NSMinXEdge];
    else
        [block setWidth:extraMargin type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockMargin edge:NSMinXEdge];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyMarginTop, result))
        [block setWidth:result + extraMargin type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockMargin edge:NSMinYEdge];
    else
        [block setWidth:extraMargin type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockMargin edge:NSMinYEdge];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyMarginRight, result))
        [block setWidth:result + extraMargin type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockMargin edge:NSMaxXEdge];
    else
        [block setWidth:extraMargin type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockMargin edge:NSMaxXEdge];
    if (_caches->floatPropertyValueForNode(element, CSSPropertyMarginBottom, result))
        [block setWidth:result + extraMargin type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockMargin edge:NSMaxYEdge];
    else
        [block setWidth:extraMargin type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockMargin edge:NSMaxYEdge];

    RetainPtr<PlatformColor> color;
    if ((color = _colorForElement(element, CSSPropertyBackgroundColor)))
        [block setBackgroundColor:color.get()];
    if (!color && backgroundColor)
        [block setBackgroundColor:backgroundColor];

    if ((color = _colorForElement(element, CSSPropertyBorderLeftColor)))
        [block setBorderColor:color.get() forEdge:NSMinXEdge];

    if ((color = _colorForElement(element, CSSPropertyBorderTopColor)))
        [block setBorderColor:color.get() forEdge:NSMinYEdge];
    if ((color = _colorForElement(element, CSSPropertyBorderRightColor)))
        [block setBorderColor:color.get() forEdge:NSMaxXEdge];
    if ((color = _colorForElement(element, CSSPropertyBorderBottomColor)))
        [block setBorderColor:color.get() forEdge:NSMaxYEdge];
}

static inline BOOL read2DigitNumber(std::span<const char>& p, int8_t& outval)
{
    BOOL result = NO;
    char c1 = consume(p);
    if (isASCIIDigit(c1)) {
        char c2 = consume(p);
        if (isASCIIDigit(c2)) {
            outval = 10 * (c1 - '0') + (c2 - '0');
            result = YES;
        }
    }
    return result;
}

static inline NSDate *_dateForString(NSString *string)
{
    auto p = unsafeSpanIncludingNullTerminator([string UTF8String]);
    RetainPtr<NSDateComponents> dateComponents = adoptNS([[NSDateComponents alloc] init]);

    // Set the time zone to GMT
    [dateComponents setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];

    NSInteger year = 0;
    while (p.front() && isASCIIDigit(p.front()))
        year = 10 * year + consume(p) - '0';
    if (consume(p) != '-')
        return nil;
    [dateComponents setYear:year];

    int8_t component;
    if (!read2DigitNumber(p, component) || consume(p) != '-')
        return nil;
    [dateComponents setMonth:component];

    if (!read2DigitNumber(p, component) || consume(p) != 'T')
        return nil;
    [dateComponents setDay:component];

    if (!read2DigitNumber(p, component) || consume(p) != ':')
        return nil;
    [dateComponents setHour:component];

    if (!read2DigitNumber(p, component) || consume(p) != ':')
        return nil;
    [dateComponents setMinute:component];

    if (!read2DigitNumber(p, component) || consume(p) != 'Z')
        return nil;
    [dateComponents setSecond:component];

    auto calendar = adoptNS([[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian]);
    return [calendar dateFromComponents:dateComponents.get()];
}

static NSInteger _colCompare(id block1, id block2, void *)
{
    NSInteger col1 = [(NSTextTableBlock *)block1 startingColumn];
    NSInteger col2 = [(NSTextTableBlock *)block2 startingColumn];
    return ((col1 < col2) ? NSOrderedAscending : ((col1 == col2) ? NSOrderedSame : NSOrderedDescending));
}

void HTMLConverter::_processMetaElementWithName(NSString *name, NSString *content)
{
    NSString *key = nil;
    if (NSOrderedSame == [@"CocoaVersion" compare:name options:NSCaseInsensitiveSearch]) {
        CGFloat versionNumber = [content doubleValue];
        if (versionNumber > 0.0) {
            // ??? this should be keyed off of version number in future
            [_documentAttrs removeObjectForKey:NSConvertedDocumentAttribute];
            [_documentAttrs setObject:@(versionNumber) forKey:NSCocoaVersionDocumentAttribute];
        }
#if PLATFORM(IOS_FAMILY)
    } else if (NSOrderedSame == [@"Generator" compare:name options:NSCaseInsensitiveSearch]) {
        key = NSGeneratorDocumentAttribute;
#endif
    } else if (NSOrderedSame == [@"Keywords" compare:name options:NSCaseInsensitiveSearch]) {
        if (content && [content length] > 0) {
            NSArray *array;
            // ??? need better handling here and throughout
            if ([content rangeOfString:@", "].length == 0 && [content rangeOfString:@","].length > 0)
                array = [content componentsSeparatedByString:@","];
            else if ([content rangeOfString:@", "].length == 0 && [content rangeOfString:@" "].length > 0)
                array = [content componentsSeparatedByString:@" "];
            else
                array = [content componentsSeparatedByString:@", "];
            [_documentAttrs setObject:array forKey:NSKeywordsDocumentAttribute];
        }
    } else if (NSOrderedSame == [@"Author" compare:name options:NSCaseInsensitiveSearch])
        key = NSAuthorDocumentAttribute;
    else if (NSOrderedSame == [@"LastAuthor" compare:name options:NSCaseInsensitiveSearch])
        key = NSEditorDocumentAttribute;
    else if (NSOrderedSame == [@"Company" compare:name options:NSCaseInsensitiveSearch])
        key = NSCompanyDocumentAttribute;
    else if (NSOrderedSame == [@"Copyright" compare:name options:NSCaseInsensitiveSearch])
        key = NSCopyrightDocumentAttribute;
    else if (NSOrderedSame == [@"Subject" compare:name options:NSCaseInsensitiveSearch])
        key = NSSubjectDocumentAttribute;
    else if (NSOrderedSame == [@"Description" compare:name options:NSCaseInsensitiveSearch] || NSOrderedSame == [@"Comment" compare:name options:NSCaseInsensitiveSearch])
        key = NSCommentDocumentAttribute;
    else if (NSOrderedSame == [@"CreationTime" compare:name options:NSCaseInsensitiveSearch]) {
        if (content && [content length] > 0) {
            NSDate *date = _dateForString(content);
            if (date)
                [_documentAttrs setObject:date forKey:NSCreationTimeDocumentAttribute];
        }
    } else if (NSOrderedSame == [@"ModificationTime" compare:name options:NSCaseInsensitiveSearch]) {
        if (content && [content length] > 0) {
            NSDate *date = _dateForString(content);
            if (date)
                [_documentAttrs setObject:date forKey:NSModificationTimeDocumentAttribute];
        }
    }
#if PLATFORM(IOS_FAMILY)
    else if (NSOrderedSame == [@"DisplayName" compare:name options:NSCaseInsensitiveSearch] || NSOrderedSame == [@"IndexTitle" compare:name options:NSCaseInsensitiveSearch])
        key = NSDisplayNameDocumentAttribute;
    else if (NSOrderedSame == [@"robots" compare:name options:NSCaseInsensitiveSearch]) {
        if ([content rangeOfString:@"noindex" options:NSCaseInsensitiveSearch].length > 0)
            [_documentAttrs setObject:[NSNumber numberWithInteger:1] forKey:NSNoIndexDocumentAttribute];
    }
#endif
    if (key && content && [content length] > 0)
        [_documentAttrs setObject:content forKey:key];
}

void HTMLConverter::_processHeadElement(Element& element)
{
    // FIXME: Should gather data from other sources e.g. Word, but for that we would need to be able to get comments from DOM

    for (HTMLMetaElement* child = Traversal<HTMLMetaElement>::firstChild(element); child; child = Traversal<HTMLMetaElement>::nextSibling(*child)) {
        RetainPtr name = child->name().createNSString();
        RetainPtr content = child->content().createNSString();
        if (name && content)
            _processMetaElementWithName(name.get(), content.get());
    }
}

void HTMLConverter::_enterBlockquote()
{
    _topPresentationIntent = [NSPresentationIntent blockQuoteIntentWithIdentity:++_topPresentationIntentIdentity nestedInsideIntent:_topPresentationIntent.get()];
}

void HTMLConverter::_exitBlockquote()
{
    if (_topPresentationIntent)
        _topPresentationIntent = [_topPresentationIntent parentIntent];
}

BOOL HTMLConverter::_enterElement(Element& element, BOOL embedded)
{
    String displayValue = _caches->propertyValueForNode(element, CSSPropertyDisplay);
    if (element.hasTagName(blockquoteTag))
        _enterBlockquote();

    if (element.hasTagName(headTag) && !embedded)
        _processHeadElement(element);
    else if ((!m_ignoreUserSelectNoneContent || !m_userSelectNoneStateCache.nodeOnlyContainsUserSelectNone(element)) && (!displayValue.length() || !(displayValue == noneAtom() || displayValue == "table-column"_s || displayValue == "table-column-group"_s))) {
        if (_caches->isBlockElement(element) && !element.hasTagName(brTag) && !(displayValue == "table-cell"_s && ![_textTables count])
            && !([_textLists count] > 0 && displayValue == "block"_s && !element.hasTagName(liTag) && !element.hasTagName(ulTag) && !element.hasTagName(olTag)))
            _newParagraphForElement(element, element.tagName().createNSString().get(), NO, YES);
        return YES;
    }
    return NO;
}

void HTMLConverter::_addLinkForElement(Element& element, NSRange range)
{
#if ENABLE(DATA_DETECTION)
    if (DataDetection::isDataDetectorElement(element))
        return;
#endif

    RetainPtr urlString = element.getAttribute(hrefAttr).createNSString();
    RetainPtr strippedString = [urlString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (urlString && [urlString length] > 0 && strippedString && [strippedString length] > 0 && ![strippedString hasPrefix:@"#"]) {
        RetainPtr url = element.document().completeURL(urlString.get()).createNSURL();
        if (!url)
            url = element.document().completeURL(strippedString.get()).createNSURL();
        if (!url)
            url = [NSURL _web_URLWithString:strippedString.get() relativeToURL:nil];
        [_attrStr addAttribute:NSLinkAttributeName value:url ? (id)url.get() : (id)urlString.get() range:range];
    }
}

void HTMLConverter::_addTableForElement(Element *tableElement)
{
    RetainPtr<NSTextTable> table = adoptNS([(NSTextTable *)[PlatformNSTextTable alloc] init]);
    CGFloat cellSpacingVal = 1;
    CGFloat cellPaddingVal = 1;
    [table setNumberOfColumns:1];
    [table setLayoutAlgorithm:NSTextTableAutomaticLayoutAlgorithm];
    [table setCollapsesBorders:NO];
    [table setHidesEmptyCells:NO];

    if (tableElement) {
        ASSERT(tableElement);
        Element& coreTableElement = *tableElement;

        RetainPtr cellSpacing = coreTableElement.getAttribute(cellspacingAttr).createNSString();
        if (cellSpacing && [cellSpacing length] > 0 && ![cellSpacing hasSuffix:@"%"])
            cellSpacingVal = [cellSpacing floatValue];
        RetainPtr cellPadding = coreTableElement.getAttribute(cellpaddingAttr).createNSString();
        if (cellPadding && [cellPadding length] > 0 && ![cellPadding hasSuffix:@"%"])
            cellPaddingVal = [cellPadding floatValue];

        _fillInBlock(table.get(), coreTableElement, nil, 0, 0, YES);

        if (_caches->propertyValueForNode(coreTableElement, CSSPropertyBorderCollapse) == "collapse"_s) {
            [table setCollapsesBorders:YES];
            cellSpacingVal = 0;
        }
        if (_caches->propertyValueForNode(coreTableElement, CSSPropertyEmptyCells) == "hide"_s)
            [table setHidesEmptyCells:YES];
        if (_caches->propertyValueForNode(coreTableElement, CSSPropertyTableLayout) == "fixed"_s)
            [table setLayoutAlgorithm:NSTextTableFixedLayoutAlgorithm];
    }

    [_textTables addObject:table.get()];
    [_textTableSpacings addObject:@(cellSpacingVal)];
    [_textTablePaddings addObject:@(cellPaddingVal)];
    [_textTableRows addObject:[NSNumber numberWithInteger:0]];
    [_textTableRowArrays addObject:[NSMutableArray array]];
}

void HTMLConverter::_addTableCellForElement(Element* element)
{
    NSTextTable *table = [_textTables lastObject];
    NSInteger rowNumber = [[_textTableRows lastObject] integerValue];
    NSInteger columnNumber = 0;
    NSInteger rowSpan = 1;
    NSInteger colSpan = 1;
    NSMutableArray *rowArray = [_textTableRowArrays lastObject];
    NSUInteger count = [rowArray count];
    PlatformColor *color = ([_textTableRowBackgroundColors count] > 0) ? [_textTableRowBackgroundColors lastObject] : nil;
    NSTextTableBlock *previousBlock;
    CGFloat cellSpacingVal = [[_textTableSpacings lastObject] floatValue];
    if ([color isEqual:[PlatformColorClass clearColor]]) color = nil;
    for (NSUInteger i = 0; i < count; i++) {
        previousBlock = [rowArray objectAtIndex:i];
        if (columnNumber >= [previousBlock startingColumn] && columnNumber < [previousBlock startingColumn] + [previousBlock columnSpan])
            columnNumber = [previousBlock startingColumn] + [previousBlock columnSpan];
    }

    RetainPtr<NSTextTableBlock> block;

    if (element) {
        if (RefPtr tableCellElement = dynamicDowncast<HTMLTableCellElement>(*element)) {
            rowSpan = tableCellElement->rowSpan();
            if (rowSpan < 1)
                rowSpan = 1;
            colSpan = tableCellElement->colSpan();
            if (colSpan < 1)
                colSpan = 1;
        }

        block = adoptNS([[PlatformNSTextTableBlock alloc] initWithTable:table startingRow:rowNumber rowSpan:rowSpan startingColumn:columnNumber columnSpan:colSpan]);

        String verticalAlign = _caches->propertyValueForNode(*element, CSSPropertyVerticalAlign);

        _fillInBlock(block.get(), *element, color, cellSpacingVal / 2, 0, NO);
        if (verticalAlign == "middle"_s)
            [block setVerticalAlignment:NSTextBlockMiddleAlignment];
        else if (verticalAlign == "bottom"_s)
            [block setVerticalAlignment:NSTextBlockBottomAlignment];
        else if (verticalAlign == "baseline"_s)
            [block setVerticalAlignment:NSTextBlockBaselineAlignment];
        else if (verticalAlign == "top"_s)
            [block setVerticalAlignment:NSTextBlockTopAlignment];
    } else {
        block = adoptNS([[PlatformNSTextTableBlock alloc] initWithTable:table startingRow:rowNumber rowSpan:rowSpan startingColumn:columnNumber columnSpan:colSpan]);
    }

    [_textBlocks addObject:block.get()];
    [rowArray addObject:block.get()];
    [rowArray sortUsingFunction:_colCompare context:NULL];
}

BOOL HTMLConverter::_processElement(Element& element, NSInteger depth)
{
    BOOL retval = YES;
    BOOL isBlockLevel = _caches->isBlockElement(element);
    String displayValue = _caches->propertyValueForNode(element, CSSPropertyDisplay);
    if (isBlockLevel)
        [_writingDirectionArray removeAllObjects];
    else {
        String bidi = _caches->propertyValueForNode(element, CSSPropertyUnicodeBidi);
        if (bidi == "embed"_s) {
            NSUInteger val = NSWritingDirectionEmbedding;
            if (_caches->propertyValueForNode(element, CSSPropertyDirection) == "rtl"_s)
                val |= NSWritingDirectionRightToLeft;
            [_writingDirectionArray addObject:[NSNumber numberWithUnsignedInteger:val]];
        } else if (bidi == "bidi-override"_s) {
            NSUInteger val = NSWritingDirectionOverride;
            if (_caches->propertyValueForNode(element, CSSPropertyDirection) == "rtl"_s)
                val |= NSWritingDirectionRightToLeft;
            [_writingDirectionArray addObject:[NSNumber numberWithUnsignedInteger:val]];
        }
    }
    if (displayValue == "table"_s || (![_textTables count] && displayValue == "table-row-group"_s)) {
        Element* tableElement = &element;
        if (displayValue == "table-row-group"_s) {
            // If we are starting in medias res, the first thing we see may be the tbody, so go up to the table
            tableElement = _blockLevelElementForNode(element.parentInComposedTree());
            if (!tableElement || _caches->propertyValueForNode(*tableElement, CSSPropertyDisplay) != "table"_s)
                tableElement = &element;
        }
        while ([_textTables count] > [_textBlocks count])
            _addTableCellForElement(nil);
        _addTableForElement(tableElement);
    } else if (displayValue == "table-footer-group"_s && [_textTables count] > 0) {
        m_textTableFooters.add((__bridge CFTypeRef)[_textTables lastObject], &element);
        retval = NO;
    } else if (displayValue == "table-row"_s && [_textTables count] > 0) {
        auto color = _colorForElement(element, CSSPropertyBackgroundColor);
        if (!color)
            color = (PlatformColor *)[PlatformColorClass clearColor];
        [_textTableRowBackgroundColors addObject:color.get()];
    } else if (displayValue == "table-cell"_s) {
        while ([_textTables count] < [_textBlocks count] + 1)
            _addTableForElement(nil);
        _addTableCellForElement(&element);
#if ENABLE(ATTACHMENT_ELEMENT)
    } else if (RefPtr attachment = dynamicDowncast<HTMLAttachmentElement>(element)) {
        if (attachment->file()) {
            RetainPtr url = [NSURL fileURLWithPath:attachment->file()->path().createNSString().get()];
            if (url)
                _addAttachmentForElement(element, url.get(), isBlockLevel, NO);
        }
        retval = NO;
#endif
    } else if (RefPtr imageElement = dynamicDowncast<HTMLImageElement>(element)) {
#if ENABLE(MULTI_REPRESENTATION_HEIC)
        if (imageElement->isMultiRepresentationHEIC())
            retval = !_addMultiRepresentationHEICAttachmentForImageElement(*imageElement);
#endif
        RetainPtr urlString = element.imageSourceURL().createNSString();
        if (retval && urlString && [urlString length] > 0) {
            RetainPtr url = element.document().completeURL(urlString.get()).createNSURL();
            if (!url)
                url = [NSURL _web_URLWithString:[urlString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] relativeToURL:nil];
#if PLATFORM(IOS_FAMILY)
            BOOL usePlaceholderImage = NO;
#else
            BOOL usePlaceholderImage = YES;
#endif
            if (url)
                _addAttachmentForElement(element, url.get(), isBlockLevel, usePlaceholderImage);
        }
        retval = NO;
    } else if (element.hasTagName(objectTag)) {
        RetainPtr baseString = element.getAttribute(codebaseAttr).createNSString();
        RetainPtr urlString = element.getAttribute(dataAttr).createNSString();
        RetainPtr declareString = element.getAttribute(declareAttr).createNSString();
        if (urlString && [urlString length] > 0 && ![@"true" isEqualToString:declareString.get()]) {
            RetainPtr<NSURL> baseURL;
            RetainPtr<NSURL> url;
            if (baseString && [baseString length] > 0) {
                baseURL = element.document().completeURL(baseString.get()).createNSURL();
                if (!baseURL)
                    baseURL = [NSURL _web_URLWithString:[baseString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] relativeToURL:nil];
            }
            if (baseURL)
                url = [NSURL _web_URLWithString:[urlString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] relativeToURL:baseURL.get()];
            if (!url)
                url = element.document().completeURL(urlString.get()).createNSURL();
            if (!url)
                url = [NSURL _web_URLWithString:[urlString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] relativeToURL:nil];
            if (url)
                retval = !_addAttachmentForElement(element, url.get(), isBlockLevel, NO);
        }
    } else if (auto* frameElement = dynamicDowncast<HTMLFrameElementBase>(element)) {
        if (RefPtr contentDocument = frameElement->contentDocument()) {
            _traverseNode(*contentDocument, depth + 1, true /* embedded */);
            retval = NO;
        }
    } else if (element.hasTagName(brTag)) {
        Element* blockElement = _blockLevelElementForNode(element.parentInComposedTree());
        RetainPtr breakClass = element.getAttribute(classAttr).createNSString();
        RetainPtr blockTag = blockElement ? blockElement->tagName().createNSString() : nil;
        BOOL isExtraBreak = [AppleInterchangeNewline.createNSString() isEqualToString:breakClass.get()];
        BOOL blockElementIsParagraph = ([@"P" isEqualToString:blockTag.get()] || [@"LI" isEqualToString:blockTag.get()] || ([blockTag hasPrefix:@"H"] && 2 == [blockTag length]));
        if (isExtraBreak)
            _flags.hasTrailingNewline = YES;
        else {
            if (blockElement && blockElementIsParagraph)
                _newLineForElement(element);
            else
                _newParagraphForElement(element, element.tagName().createNSString().get(), YES, NO);
        }
    } else if (element.hasTagName(ulTag)) {
        RetainPtr<NSTextList> list;
        String listStyleType = _caches->propertyValueForNode(element, CSSPropertyListStyleType);
        if (!listStyleType.length())
            listStyleType = @"disc";
        list = adoptNS([[PlatformNSTextList alloc] initWithMarkerFormat:makeString("{"_s, listStyleType, "}"_s).createNSString().get() options:0]);
        [_textLists addObject:list.get()];
    } else if (element.hasTagName(olTag)) {
        RetainPtr<NSTextList> list;
        String listStyleType = _caches->propertyValueForNode(element, CSSPropertyListStyleType);
        if (!listStyleType.length())
            listStyleType = "decimal"_s;
        list = adoptNS([[PlatformNSTextList alloc] initWithMarkerFormat:makeString('{', listStyleType, '}').createNSString().get() options:0]);
        if (RefPtr olElement = dynamicDowncast<HTMLOListElement>(element)) {
            auto startingItemNumber = olElement->start();
            [list setStartingItemNumber:startingItemNumber];
        }
        [_textLists addObject:list.get()];
    } else if (element.hasTagName(qTag)) {
        _addQuoteForElement(element, YES, _quoteLevel++);
    } else if (element.hasTagName(inputTag)) {
        if (RefPtr inputElement = dynamicDowncast<HTMLInputElement>(element)) {
            if (inputElement->type() == textAtom()) {
                RetainPtr value = inputElement->value()->createNSString();
                if (value && [value length] > 0)
                    _addValue(value.get(), element);
            }
        }
    } else if (element.hasTagName(textareaTag)) {
        if (RefPtr textAreaElement = dynamicDowncast<HTMLTextAreaElement>(element)) {
            RetainPtr value = textAreaElement->value()->createNSString();
            if (value && [value length] > 0)
                _addValue(value.get(), element);
        }
        retval = NO;
    }
    return retval;
}

void HTMLConverter::_addMarkersToList(NSTextList *list, NSRange range)
{
    NSInteger itemNum = [list startingItemNumber];
    NSString *string = [_attrStr string];
    NSString *stringToInsert;
    NSDictionary *attrsToInsert = nil;
    NSParagraphStyle *paragraphStyle;
    NSTextTab *tab = nil;
    NSTextTab *tabToRemove;
    NSRange paragraphRange;
    NSRange styleRange;
    NSUInteger textLength = [_attrStr length];
    NSUInteger listIndex;
    NSUInteger insertLength;
    NSUInteger i;
    NSUInteger count;
    NSArray *textLists;
    CGFloat markerLocation;
    CGFloat listLocation;

    if (range.length == 0 || range.location >= textLength)
        return;
    if (NSMaxRange(range) > textLength)
        range.length = textLength - range.location;
    paragraphStyle = [_attrStr attribute:NSParagraphStyleAttributeName atIndex:range.location effectiveRange:NULL];
    if (paragraphStyle) {
        textLists = [paragraphStyle textLists];
        listIndex = [textLists indexOfObject:list];
        if (textLists && listIndex != NSNotFound) {
            for (NSUInteger idx = range.location; idx < NSMaxRange(range);) {
                paragraphRange = [string paragraphRangeForRange:NSMakeRange(idx, 0)];
                paragraphStyle = [_attrStr attribute:NSParagraphStyleAttributeName atIndex:idx effectiveRange:&styleRange];
                if ([[paragraphStyle textLists] count] == listIndex + 1) {
                    stringToInsert = [NSString stringWithFormat:@"\t%@\t", [list markerForItemNumber:itemNum++]];
                    insertLength = [stringToInsert length];
                    attrsToInsert = [PlatformNSTextList _standardMarkerAttributesForAttributes:[_attrStr attributesAtIndex:paragraphRange.location effectiveRange:NULL]];

                    [_attrStr replaceCharactersInRange:NSMakeRange(paragraphRange.location, 0) withString:stringToInsert];
                    [_attrStr setAttributes:attrsToInsert range:NSMakeRange(paragraphRange.location, insertLength)];
                    range.length += insertLength;
                    paragraphRange.length += insertLength;
                    if (paragraphRange.location < _domRangeStartIndex)
                        _domRangeStartIndex += insertLength;

                    auto newStyle = adoptNS([paragraphStyle mutableCopy]);
                    listLocation = (listIndex + 1) * 36;
                    markerLocation = listLocation - 25;
                    [newStyle setFirstLineHeadIndent:0];
                    [newStyle setHeadIndent:listLocation];
                    while ((count = [[newStyle tabStops] count]) > 0) {
                        for (i = 0, tabToRemove = nil; !tabToRemove && i < count; i++) {
                            tab = [[newStyle tabStops] objectAtIndex:i];
                            if ([tab location] <= listLocation)
                                tabToRemove = tab;
                        }
                        if (tabToRemove)
                            [newStyle removeTabStop:tab];
                        else
                            break;
                    }
                    [newStyle addTabStop:adoptNS([[PlatformNSTextTab alloc] initWithType:NSLeftTabStopType location:markerLocation]).get()];
                    [newStyle addTabStop:adoptNS([[PlatformNSTextTab alloc] initWithTextAlignment:NSTextAlignmentNatural location:listLocation options:@{ }]).get()];
                    [_attrStr addAttribute:NSParagraphStyleAttributeName value:newStyle.get() range:paragraphRange];

                    idx = NSMaxRange(paragraphRange);
                } else {
                    // skip any deeper-nested lists
                    idx = NSMaxRange(styleRange);
                }
            }
        }
    }
}

void HTMLConverter::_exitElement(Element& element, NSInteger depth, NSUInteger startIndex)
{
    String displayValue = _caches->propertyValueForNode(element, CSSPropertyDisplay);
    NSRange range = NSMakeRange(startIndex, [_attrStr length] - startIndex);
    if (range.length > 0 && element.hasTagName(aTag))
        _addLinkForElement(element, range);
    if (!_flags.reachedEnd && _caches->isBlockElement(element)) {
        [_writingDirectionArray removeAllObjects];
        if (displayValue == "table-cell"_s && ![_textBlocks count]) {
            _newTabForElement(element);
        } else if ([_textLists count] > 0 && displayValue == "block"_s && !element.hasTagName(liTag) && !element.hasTagName(ulTag) && !element.hasTagName(olTag)) {
            _newLineForElement(element);
        } else {
            _newParagraphForElement(element, element.tagName().createNSString().get(), !range.length, YES);
        }
    } else if ([_writingDirectionArray count] > 0) {
        String bidi = _caches->propertyValueForNode(element, CSSPropertyUnicodeBidi);
        if (bidi == "embed"_s || bidi == "bidi-override"_s)
            [_writingDirectionArray removeLastObject];
    }
    range = NSMakeRange(startIndex, [_attrStr length] - startIndex);
    if (displayValue == "table"_s && [_textTables count] > 0) {
        NSTextTable *key = [_textTables lastObject];
        Element* footer = m_textTableFooters.get((__bridge CFTypeRef)key);
        while ([_textTables count] < [_textBlocks count] + 1)
            [_textBlocks removeLastObject];
        if (footer) {
            _traverseFooterNode(*footer, depth + 1);
            m_textTableFooters.remove((__bridge CFTypeRef)key);
        }
        [_textTables removeLastObject];
        [_textTableSpacings removeLastObject];
        [_textTablePaddings removeLastObject];
        [_textTableRows removeLastObject];
        [_textTableRowArrays removeLastObject];
    } else if (displayValue == "table-row"_s && [_textTables count] > 0) {
        NSTextTable *table = [_textTables lastObject];
        NSTextTableBlock *block;
        NSMutableArray *rowArray = [_textTableRowArrays lastObject], *previousRowArray;
        NSUInteger i, count;
        NSInteger numberOfColumns = [table numberOfColumns];
        NSInteger openColumn;
        NSInteger rowNumber = [[_textTableRows lastObject] integerValue];
        do {
            rowNumber++;
            previousRowArray = rowArray;
            rowArray = [NSMutableArray array];
            count = [previousRowArray count];
            for (i = 0; i < count; i++) {
                block = [previousRowArray objectAtIndex:i];
                if ([block startingColumn] + [block columnSpan] > numberOfColumns) numberOfColumns = [block startingColumn] + [block columnSpan];
                if ([block startingRow] + [block rowSpan] > rowNumber) [rowArray addObject:block];
            }
            count = [rowArray count];
            openColumn = 0;
            for (i = 0; i < count; i++) {
                block = [rowArray objectAtIndex:i];
                if (openColumn >= [block startingColumn] && openColumn < [block startingColumn] + [block columnSpan]) openColumn = [block startingColumn] + [block columnSpan];
            }
        } while (openColumn >= numberOfColumns);
        if ((NSUInteger)numberOfColumns > [table numberOfColumns])
            [table setNumberOfColumns:numberOfColumns];
        [_textTableRows removeLastObject];
        [_textTableRows addObject:[NSNumber numberWithInteger:rowNumber]];
        [_textTableRowArrays removeLastObject];
        [_textTableRowArrays addObject:rowArray];
        if ([_textTableRowBackgroundColors count] > 0)
            [_textTableRowBackgroundColors removeLastObject];
    } else if (displayValue == "table-cell"_s && [_textBlocks count] > 0) {
        while ([_textTables count] > [_textBlocks count]) {
            [_textTables removeLastObject];
            [_textTableSpacings removeLastObject];
            [_textTablePaddings removeLastObject];
            [_textTableRows removeLastObject];
            [_textTableRowArrays removeLastObject];
        }
        [_textBlocks removeLastObject];
    } else if ((element.hasTagName(ulTag) || element.hasTagName(olTag)) && [_textLists count] > 0) {
        NSTextList *list = [_textLists lastObject];
        _addMarkersToList(list, range);
        [_textLists removeLastObject];
    } else if (element.hasTagName(qTag)) {
        _addQuoteForElement(element, NO, --_quoteLevel);
    } else if (element.hasTagName(spanTag)) {
        RetainPtr className = element.getAttribute(classAttr).createNSString();
        NSMutableString *mutableString;
        NSUInteger i, count = 0;
        unichar c;
        if ([@"Apple-converted-space" isEqualToString:className.get()]) {
            mutableString = [_attrStr mutableString];
            for (i = range.location; i < NSMaxRange(range); i++) {
                c = [mutableString characterAtIndex:i];
                if (0xa0 == c)
                    [mutableString replaceCharactersInRange:NSMakeRange(i, 1) withString:@" "];
            }
        } else if ([@"Apple-converted-tab" isEqualToString:className.get()]) {
            mutableString = [_attrStr mutableString];
            for (i = range.location; i < NSMaxRange(range); i++) {
                NSRange rangeToReplace = NSMakeRange(NSNotFound, 0);
                c = [mutableString characterAtIndex:i];
                if (' ' == c || 0xa0 == c) {
                    count++;
                    if (count >= 4 || i + 1 >= NSMaxRange(range))
                        rangeToReplace = NSMakeRange(i + 1 - count, count);
                } else {
                    if (count > 0)
                        rangeToReplace = NSMakeRange(i - count, count);
                }
                if (rangeToReplace.length > 0) {
                    [mutableString replaceCharactersInRange:rangeToReplace withString:@"\t"];
                    range.length -= (rangeToReplace.length - 1);
                    i -= (rangeToReplace.length - 1);
                    if (NSMaxRange(rangeToReplace) <= _domRangeStartIndex) {
                        _domRangeStartIndex -= (rangeToReplace.length - 1);
                    } else if (rangeToReplace.location < _domRangeStartIndex) {
                        _domRangeStartIndex = rangeToReplace.location;
                    }
                    count = 0;
                }
            }
        }
    }

    if (element.hasTagName(blockquoteTag))
        _exitBlockquote();
}

void HTMLConverter::_processText(Text& text)
{
    if (m_ignoreUserSelectNoneContent && m_userSelectNoneStateCache.nodeOnlyContainsUserSelectNone(text))
        return;
    NSUInteger textLength = [_attrStr length];
    unichar lastChar = (textLength > 0) ? [[_attrStr string] characterAtIndex:textLength - 1] : '\n';
    BOOL suppressLeadingSpace = ((_flags.isSoft && lastChar == ' ') || lastChar == '\n' || lastChar == '\r' || lastChar == '\t' || lastChar == NSParagraphSeparatorCharacter || lastChar == NSLineSeparatorCharacter || lastChar == NSFormFeedCharacter || lastChar == WebNextLineCharacter);
    NSRange rangeToReplace = NSMakeRange(textLength, 0);

    String originalString = text.data();
    unsigned startOffset = 0;
    unsigned endOffset = originalString.length();
    if (&text == m_start.containerNode()) {
        startOffset = m_start.offsetInContainerNode();
        _domRangeStartIndex = [_attrStr length];
        _flags.reachedStart = YES;
    }
    if (&text == m_end.containerNode()) {
        endOffset = m_end.offsetInContainerNode();
        _flags.reachedEnd = YES;
    }
    if ((startOffset > 0 || endOffset < originalString.length()) && endOffset >= startOffset)
        originalString = originalString.substring(startOffset, endOffset - startOffset);
    String outputString = originalString;

    // FIXME: Use RenderText's content instead.
    bool wasSpace = false;
    if (_caches->propertyValueForNode(text, CSSPropertyWhiteSpace).startsWith("pre"_s)) {
        if (textLength && originalString.length() && _flags.isSoft) {
            unichar c = originalString.characterAt(0);
            if (c == '\n' || c == '\r' || c == NSParagraphSeparatorCharacter || c == NSLineSeparatorCharacter || c == NSFormFeedCharacter || c == WebNextLineCharacter)
                rangeToReplace = NSMakeRange(textLength - 1, 1);
        }
    } else {
        unsigned count = originalString.length();
        bool wasLeading = true;
        StringBuilder builder;
        LChar noBreakSpaceRepresentation = 0;
        for (unsigned i = 0; i < count; i++) {
            char16_t c = originalString.characterAt(i);
            bool isWhitespace = c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0xc || c == 0x200b;
            if (isWhitespace)
                wasSpace = (!wasLeading || !suppressLeadingSpace);
            else {
                if (wasSpace)
                    builder.append(' ');
                if (c != noBreakSpace)
                    builder.append(c);
                else {
                    if (!noBreakSpaceRepresentation)
                        noBreakSpaceRepresentation = _caches->propertyValueForNode(text, CSSPropertyWebkitNbspMode) == "space"_s ? ' ' : noBreakSpace;
                    builder.append(noBreakSpaceRepresentation);
                }
                wasSpace = false;
                wasLeading = false;
            }
        }
        if (wasSpace)
            builder.append(' ');
        outputString = builder.toString();
    }

    if (outputString.length()) {
        String textTransform = _caches->propertyValueForNode(text, CSSPropertyTextTransform);
        if (textTransform == "capitalize"_s)
            outputString = capitalize(outputString); // FIXME: Needs to take locale into account to work correctly.
        else if (textTransform == "uppercase"_s)
            outputString = outputString.convertToUppercaseWithoutLocale(); // FIXME: Needs locale to work correctly.
        else if (textTransform == "lowercase"_s)
            outputString = outputString.convertToLowercaseWithoutLocale(); // FIXME: Needs locale to work correctly.

        [_attrStr replaceCharactersInRange:rangeToReplace withString:outputString.createNSString().get()];
        rangeToReplace.length = outputString.length();
        if (rangeToReplace.length)
            [_attrStr setAttributes:aggregatedAttributesForAncestors(text) range:rangeToReplace];
        _flags.isSoft = wasSpace;
    }
}

void HTMLConverter::_traverseNode(Node& node, unsigned depth, bool embedded)
{
    if (_flags.reachedEnd)
        return;
    if (!_flags.reachedStart && !_caches->isAncestorsOfStartToBeConverted(node))
        return;

    unsigned startOffset = 0;
    unsigned endOffset = UINT_MAX;
    bool isStart = false;
    bool isEnd = false;
    if (&node == m_start.containerNode()) {
        startOffset = m_start.computeOffsetInContainerNode();
        isStart = true;
        _flags.reachedStart = YES;
    }
    if (&node == m_end.containerNode()) {
        endOffset = m_end.computeOffsetInContainerNode();
        isEnd = true;
    }

    if (node.isDocumentNode() || node.isDocumentFragment()) {
        Node* child = node.firstChild();
        ASSERT(child == firstChildInComposedTreeIgnoringUserAgentShadow(node));
        for (NSUInteger i = 0; child; i++) {
            if (isStart && i == startOffset)
                _domRangeStartIndex = [_attrStr length];
            if ((!isStart || startOffset <= i) && (!isEnd || endOffset > i))
                _traverseNode(*child, depth + 1, embedded);
            if (isEnd && i + 1 >= endOffset)
                _flags.reachedEnd = YES;
            if (_flags.reachedEnd)
                break;
            ASSERT(child->nextSibling() == nextSiblingInComposedTreeIgnoringUserAgentShadow(*child));
            child = child->nextSibling();
        }
    } else if (RefPtr element = dynamicDowncast<Element>(node)) {
        if (_enterElement(*element, embedded)) {
            NSUInteger startIndex = [_attrStr length];
            if (_processElement(*element, depth)) {
                if (auto* shadowRoot = shadowRootIgnoringUserAgentShadow(*element)) // Traverse through shadow root to detect start and end.
                    _traverseNode(*shadowRoot, depth + 1, embedded);
                else {
                    auto* child = firstChildInComposedTreeIgnoringUserAgentShadow(node);
                    for (NSUInteger i = 0; child; i++) {
                        if (isStart && i == startOffset)
                            _domRangeStartIndex = [_attrStr length];
                        if ((!isStart || startOffset <= i) && (!isEnd || endOffset > i))
                            _traverseNode(*child, depth + 1, embedded);
                        if (isEnd && i + 1 >= endOffset)
                            _flags.reachedEnd = YES;
                        if (_flags.reachedEnd)
                            break;
                        child = nextSiblingInComposedTreeIgnoringUserAgentShadow(*child);
                    }
                }
                _exitElement(*element, depth, std::min(startIndex, [_attrStr length]));
            }
        }
    } else if (RefPtr text = dynamicDowncast<Text>(node))
        _processText(*text);

    if (isEnd)
        _flags.reachedEnd = YES;
}

void HTMLConverter::_traverseFooterNode(Element& element, unsigned depth)
{
    if (_flags.reachedEnd)
        return;
    if (!_flags.reachedStart && !_caches->isAncestorsOfStartToBeConverted(element))
        return;

    unsigned startOffset = 0;
    unsigned endOffset = UINT_MAX;
    bool isStart = false;
    bool isEnd = false;
    if (&element == m_start.containerNode()) {
        startOffset = m_start.computeOffsetInContainerNode();
        isStart = true;
        _flags.reachedStart = YES;
    }
    if (&element == m_end.containerNode()) {
        endOffset = m_end.computeOffsetInContainerNode();
        isEnd = true;
    }

    if (_enterElement(element, YES)) {
        NSUInteger startIndex = [_attrStr length];
        if (_processElement(element, depth)) {
            auto* child = firstChildInComposedTreeIgnoringUserAgentShadow(element);
            for (NSUInteger i = 0; child; i++) {
                if (isStart && i == startOffset)
                    _domRangeStartIndex = [_attrStr length];
                if ((!isStart || startOffset <= i) && (!isEnd || endOffset > i))
                    _traverseNode(*child, depth + 1, true /* embedded */);
                if (isEnd && i + 1 >= endOffset)
                    _flags.reachedEnd = YES;
                if (_flags.reachedEnd)
                    break;
                child = nextSiblingInComposedTreeIgnoringUserAgentShadow(*child);
            }
            _exitElement(element, depth, startIndex);
        }
    }
    if (isEnd)
        _flags.reachedEnd = YES;
}

void HTMLConverter::_adjustTrailingNewline()
{
    NSUInteger textLength = [_attrStr length];
    unichar lastChar = (textLength > 0) ? [[_attrStr string] characterAtIndex:textLength - 1] : 0;
    BOOL alreadyHasTrailingNewline = (lastChar == '\n' || lastChar == '\r' || lastChar == NSParagraphSeparatorCharacter || lastChar == NSLineSeparatorCharacter || lastChar == WebNextLineCharacter);
    if (_flags.hasTrailingNewline && !alreadyHasTrailingNewline)
        [_attrStr replaceCharactersInRange:NSMakeRange(textLength, 0) withString:@"\n"];
}

Node* HTMLConverterCaches::cacheAncestorsOfStartToBeConverted(const Position& start, const Position& end)
{
    auto commonAncestor = commonInclusiveAncestor(start, end);
    Node* ancestor = start.containerNode();

    while (ancestor) {
        m_ancestorsUnderCommonAncestor.add(*ancestor);
        if (ancestor == commonAncestor)
            break;
        ancestor = ancestor->parentInComposedTree();
    }

    return commonAncestor;
}

namespace WebCore {

// This function supports more HTML features than the editing variant below, such as tables.
AttributedString attributedString(const SimpleRange& range, IgnoreUserSelectNone treatment)
{
    return HTMLConverter { range, treatment }.convert();
}

}
