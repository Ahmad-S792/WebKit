/*
 * Copyright (C) 2008, 2009, 2010, 2011 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "WebAccessibilityObjectWrapperBase.h"

#import "AXCoreObject.h"
#import "AXIsolatedObject.h"
#import "AXObjectCache.h"
#import "AXRemoteFrame.h"
#import "AXSearchManager.h"
#import "AccessibilityList.h"
#import "AccessibilityListBox.h"
#import "AccessibilityRenderObject.h"
#import "AccessibilityScrollView.h"
#import "AccessibilitySpinButton.h"
#import "AccessibilityTable.h"
#import "AccessibilityTableCell.h"
#import "AccessibilityTableColumn.h"
#import "AccessibilityTableRow.h"
#import "BoundaryPointInlines.h"
#import "ColorMac.h"
#import "ContextMenuController.h"
#import "Editing.h"
#import "FrameDestructionObserverInlines.h"
#import "FrameInlines.h"
#import "FrameSelection.h"
#import "LayoutRect.h"
#import "LocalizedStrings.h"
#import "Page.h"
#import "RenderTextControl.h"
#import "RenderView.h"
#import "RenderWidget.h"
#import "ScrollView.h"
#import "TextCheckerClient.h"
#import "TextIterator.h"
#import "VisibleUnits.h"
#import <Accessibility/Accessibility.h>
#import <wtf/ObjCRuntimeExtras.h>
#import <wtf/cocoa/VectorCocoa.h>
#import <pal/cocoa/AccessibilitySoftLink.h>

#if PLATFORM(MAC)
#import "WebAccessibilityObjectWrapperMac.h"
#else
#import "WebAccessibilityObjectWrapperIOS.h"
#endif

using namespace WebCore;

// Search Keys
#ifndef NSAccessibilityAnyTypeSearchKey
#define NSAccessibilityAnyTypeSearchKey @"AXAnyTypeSearchKey"
#endif

#ifndef NSAccessibilityArticleSearchKey
#define NSAccessibilityArticleSearchKey @"AXArticleSearchKey"
#endif

#ifndef NSAccessibilityBlockquoteSameLevelSearchKey
#define NSAccessibilityBlockquoteSameLevelSearchKey @"AXBlockquoteSameLevelSearchKey"
#endif

#ifndef NSAccessibilityBlockquoteSearchKey
#define NSAccessibilityBlockquoteSearchKey @"AXBlockquoteSearchKey"
#endif

#ifndef NSAccessibilityBoldFontSearchKey
#define NSAccessibilityBoldFontSearchKey @"AXBoldFontSearchKey"
#endif

#ifndef NSAccessibilityButtonSearchKey
#define NSAccessibilityButtonSearchKey @"AXButtonSearchKey"
#endif

#ifndef NSAccessibilityCheckBoxSearchKey
#define NSAccessibilityCheckBoxSearchKey @"AXCheckBoxSearchKey"
#endif

#ifndef NSAccessibilityControlSearchKey
#define NSAccessibilityControlSearchKey @"AXControlSearchKey"
#endif

#ifndef NSAccessibilityDifferentTypeSearchKey
#define NSAccessibilityDifferentTypeSearchKey @"AXDifferentTypeSearchKey"
#endif

#ifndef NSAccessibilityFontChangeSearchKey
#define NSAccessibilityFontChangeSearchKey @"AXFontChangeSearchKey"
#endif

#ifndef NSAccessibilityFontColorChangeSearchKey
#define NSAccessibilityFontColorChangeSearchKey @"AXFontColorChangeSearchKey"
#endif

#ifndef NSAccessibilityFrameSearchKey
#define NSAccessibilityFrameSearchKey @"AXFrameSearchKey"
#endif

#ifndef NSAccessibilityGraphicSearchKey
#define NSAccessibilityGraphicSearchKey @"AXGraphicSearchKey"
#endif

#ifndef NSAccessibilityHeadingLevel1SearchKey
#define NSAccessibilityHeadingLevel1SearchKey @"AXHeadingLevel1SearchKey"
#endif

#ifndef NSAccessibilityHeadingLevel2SearchKey
#define NSAccessibilityHeadingLevel2SearchKey @"AXHeadingLevel2SearchKey"
#endif

#ifndef NSAccessibilityHeadingLevel3SearchKey
#define NSAccessibilityHeadingLevel3SearchKey @"AXHeadingLevel3SearchKey"
#endif

#ifndef NSAccessibilityHeadingLevel4SearchKey
#define NSAccessibilityHeadingLevel4SearchKey @"AXHeadingLevel4SearchKey"
#endif

#ifndef NSAccessibilityHeadingLevel5SearchKey
#define NSAccessibilityHeadingLevel5SearchKey @"AXHeadingLevel5SearchKey"
#endif

#ifndef NSAccessibilityHeadingLevel6SearchKey
#define NSAccessibilityHeadingLevel6SearchKey @"AXHeadingLevel6SearchKey"
#endif

#ifndef NSAccessibilityHeadingSameLevelSearchKey
#define NSAccessibilityHeadingSameLevelSearchKey @"AXHeadingSameLevelSearchKey"
#endif

#ifndef NSAccessibilityHeadingSearchKey
#define NSAccessibilityHeadingSearchKey @"AXHeadingSearchKey"
#endif

#ifndef NSAccessibilityHighlightedSearchKey
#define NSAccessibilityHighlightedSearchKey @"AXHighlightedSearchKey"
#endif

#ifndef NSAccessibilityKeyboardFocusableSearchKey
#define NSAccessibilityKeyboardFocusableSearchKey @"AXKeyboardFocusableSearchKey"
#endif

#ifndef NSAccessibilityItalicFontSearchKey
#define NSAccessibilityItalicFontSearchKey @"AXItalicFontSearchKey"
#endif

#ifndef NSAccessibilityLandmarkSearchKey
#define NSAccessibilityLandmarkSearchKey @"AXLandmarkSearchKey"
#endif

#ifndef NSAccessibilityLinkSearchKey
#define NSAccessibilityLinkSearchKey @"AXLinkSearchKey"
#endif

#ifndef NSAccessibilityListSearchKey
#define NSAccessibilityListSearchKey @"AXListSearchKey"
#endif

#ifndef NSAccessibilityLiveRegionSearchKey
#define NSAccessibilityLiveRegionSearchKey @"AXLiveRegionSearchKey"
#endif

#ifndef NSAccessibilityMisspelledWordSearchKey
#define NSAccessibilityMisspelledWordSearchKey @"AXMisspelledWordSearchKey"
#endif

#ifndef NSAccessibilityOutlineSearchKey
#define NSAccessibilityOutlineSearchKey @"AXOutlineSearchKey"
#endif

#ifndef NSAccessibilityPlainTextSearchKey
#define NSAccessibilityPlainTextSearchKey @"AXPlainTextSearchKey"
#endif

#ifndef NSAccessibilityRadioGroupSearchKey
#define NSAccessibilityRadioGroupSearchKey @"AXRadioGroupSearchKey"
#endif

#ifndef NSAccessibilitySameTypeSearchKey
#define NSAccessibilitySameTypeSearchKey @"AXSameTypeSearchKey"
#endif

#ifndef NSAccessibilityStaticTextSearchKey
#define NSAccessibilityStaticTextSearchKey @"AXStaticTextSearchKey"
#endif

#ifndef NSAccessibilityStyleChangeSearchKey
#define NSAccessibilityStyleChangeSearchKey @"AXStyleChangeSearchKey"
#endif

#ifndef NSAccessibilityTableSameLevelSearchKey
#define NSAccessibilityTableSameLevelSearchKey @"AXTableSameLevelSearchKey"
#endif

#ifndef NSAccessibilityTableSearchKey
#define NSAccessibilityTableSearchKey @"AXTableSearchKey"
#endif

#ifndef NSAccessibilityTextFieldSearchKey
#define NSAccessibilityTextFieldSearchKey @"AXTextFieldSearchKey"
#endif

#ifndef NSAccessibilityUnderlineSearchKey
#define NSAccessibilityUnderlineSearchKey @"AXUnderlineSearchKey"
#endif

#ifndef NSAccessibilityUnvisitedLinkSearchKey
#define NSAccessibilityUnvisitedLinkSearchKey @"AXUnvisitedLinkSearchKey"
#endif

#ifndef NSAccessibilityVisitedLinkSearchKey
#define NSAccessibilityVisitedLinkSearchKey @"AXVisitedLinkSearchKey"
#endif

static NSArray *convertMathPairsToNSArray(const AccessibilityObject::AccessibilityMathMultiscriptPairs& pairs, NSString *subscriptKey, NSString *superscriptKey)
{
    return createNSArray(pairs, [&] (auto& pair) {
        std::array<WebAccessibilityObjectWrapper *, 2> wrappers;
        std::array<NSString *, 2> keys;
        NSUInteger count = 0;
        if (pair.first && pair.first->wrapper() && !pair.first->isIgnored()) {
            wrappers[0] = pair.first->wrapper();
            keys[0] = subscriptKey;
            count = 1;
        }
        if (pair.second && pair.second->wrapper() && !pair.second->isIgnored()) {
            wrappers[count] = pair.second->wrapper();
            keys[count] = superscriptKey;
            count += 1;
        }
        return adoptNS([[NSDictionary alloc] initWithObjects:wrappers.data() forKeys:keys.data() count:count]);
    }).autorelease();
}

NSArray *makeNSArray(const WebCore::AXCoreObject::AccessibilityChildrenVector& children, BOOL returnPlatformElements)
{
    return createNSArray(children, [returnPlatformElements] (const auto& child) -> id {
        auto wrapper = child->wrapper();
        // We want to return the attachment view instead of the object representing the attachment,
        // otherwise, we get palindrome errors in the AX hierarchy.
        if (child->isAttachment()) {
            if (id attachmentView = wrapper.attachmentView)
                return attachmentView;
        } else if (child->isRemoteFrame() && returnPlatformElements)
            return child->remoteFramePlatformElement().get();

        return wrapper;
    }).autorelease();
}

@implementation WebAccessibilityObjectWrapperBase

@synthesize identifier = _identifier;

- (id)initWithAccessibilityObject:(AccessibilityObject&)axObject
{
    ASSERT(isMainThread());

    if (!(self = [super init]))
        return nil;
    [self attachAXObject:axObject];
    return self;
}

- (void)attachAXObject:(AccessibilityObject&)axObject
{
    ASSERT(!_identifier || _identifier == axObject.objectID());
    m_axObject = axObject;
    if (!_identifier)
        _identifier = m_axObject->objectID();
}

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
- (void)attachIsolatedObject:(AXIsolatedObject*)isolatedObject
{
    ASSERT(!isMainThread());
    ASSERT(isolatedObject && (!_identifier || *_identifier == isolatedObject->objectID()));

    m_isolatedObject = isolatedObject;
    m_isolatedObjectInitialized = !!isolatedObject;

    if (!_identifier)
        _identifier = m_isolatedObject.get()->objectID();
}

- (BOOL)hasIsolatedObject
{
    return m_isolatedObjectInitialized.load();
}
#endif

- (void)detach
{
    ASSERT(isMainThread());
    _identifier = std::nullopt;
    m_axObject = nullptr;
}

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
- (void)detachIsolatedObject:(AccessibilityDetachmentType)detachmentType
{
    m_isolatedObject = nullptr;
    m_isolatedObjectInitialized = false;
}
#endif

#if PLATFORM(MAC)
- (WebCore::AXCoreObject*)updateObjectBackingStore
{
    // Calling updateBackingStore() can invalidate this element so self must be retained.
    // If it does become invalidated, self.axBackingObject will be nil.
    retainPtr(self).autorelease();

    RefPtr<AXCoreObject> backingObject = self.axBackingObject;
    if (!backingObject) {
        if (!isMainThread()) {
            // It's possible our backing object just hasn't been attached yet.
            // Try again after making sure all isolated trees are up-to-date, which could
            // attach an object to this wrapper.
            AXTreeStore<AXIsolatedTree>::applyPendingChangesForAllIsolatedTrees();
            return m_isolatedObject.get();
        }
        return nil;
    }
    backingObject->updateBackingStore();
    return self.axBackingObject;
}
#else
- (BOOL)_prepareAccessibilityCall
{
    // rdar://7980318 if we start a call, then block in WebThreadLock(), then we're dealloced on another, thread, we could
    // crash, so we should retain ourself for the duration of usage here.
    retainPtr(self).autorelease();

    WebThreadLock();

    // If we came back from our thread lock and we were detached, we will no longer have an self.axBackingObject.
    if (!self.axBackingObject)
        return NO;

    self.axBackingObject->updateBackingStore();
    if (!self.axBackingObject)
        return NO;

    return YES;
}
#endif

- (NSString *)description
{
    if (RefPtr<AXCoreObject> backingObject = self.axBackingObject) {
        NSString *backingDescription = backingObject->debugDescription().createNSString().autorelease();
        return [NSString stringWithFormat:@"wrapper %p { object %@ }", self, backingDescription];
    }
    return [NSString stringWithFormat:@"%@ (null backing object)", [super description]];
}

- (NSString *)debugDescription
{
    return [self description];
}

- (id)attachmentView
{
    return nil;
}

- (WebCore::AXCoreObject*)axBackingObject
{
    if (isMainThread())
        return m_axObject.get();

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
    ASSERT(AXObjectCache::isIsolatedTreeEnabled());
    return m_isolatedObject.get();
#else
    ASSERT_NOT_REACHED();
    return nullptr;
#endif
}

- (BOOL)isIsolatedObject
{
#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
    RefPtr backingObject = dynamicDowncast<AccessibilityObject>(self.baseUpdateBackingStore);
    return backingObject && backingObject->isAXIsolatedObjectInstance();
#else
    return NO;
#endif
}

- (NSArray<NSString *> *)baseAccessibilitySpeechHint
{
    return [self.axBackingObject->speechHint().createNSString() componentsSeparatedByString:@" "];
}

#if HAVE(ACCESSIBILITY_FRAMEWORK)
- (NSArray<AXCustomContent *> *)accessibilityCustomContent
{
    RefPtr<AXCoreObject> backingObject = [self baseUpdateBackingStore];
    if (!backingObject)
        return nil;
    
    RetainPtr<NSMutableArray<AXCustomContent *>> accessibilityCustomContent = nil;
    auto extendedDescription = backingObject->extendedDescription();
    if (extendedDescription.length()) {
        accessibilityCustomContent = adoptNS([[NSMutableArray alloc] init]);
        AXCustomContent *contentItem = [PAL::getAXCustomContentClass() customContentWithLabel:WEB_UI_STRING("description", "description detail").createNSString().get() value:extendedDescription.createNSString().get()];
        // Set this to high, so that it's always spoken.
        [contentItem setImportance:AXCustomContentImportanceHigh];
        [accessibilityCustomContent addObject:contentItem];
    }
    
    return accessibilityCustomContent.autorelease();
}
#endif

- (NSString *)baseAccessibilityHelpText
{
    return self.axBackingObject->helpTextAttributeValue().createNSString().autorelease();
}

struct PathConversionInfo {
    WebAccessibilityObjectWrapperBase *wrapper;
    CGMutablePathRef path;
};

static void convertPathToScreenSpaceFunction(PathConversionInfo& conversion, const PathElement& element)
{
    WebAccessibilityObjectWrapperBase *wrapper = conversion.wrapper;
    CGMutablePathRef newPath = conversion.path;
    FloatRect rect;
    switch (element.type) {
    case PathElement::Type::MoveToPoint:
    {
        rect = FloatRect(element.points[0], FloatSize());
        CGPoint newPoint = [wrapper convertRectToSpace:rect space:AccessibilityConversionSpace::Screen].origin;
        CGPathMoveToPoint(newPath, nil, newPoint.x, newPoint.y);
        break;
    }
    case PathElement::Type::AddLineToPoint:
    {
        rect = FloatRect(element.points[0], FloatSize());
        CGPoint newPoint = [wrapper convertRectToSpace:rect space:AccessibilityConversionSpace::Screen].origin;
        CGPathAddLineToPoint(newPath, nil, newPoint.x, newPoint.y);
        break;
    }
    case PathElement::Type::AddQuadCurveToPoint:
    {
        rect = FloatRect(element.points[0], FloatSize());
        CGPoint newPoint1 = [wrapper convertRectToSpace:rect space:AccessibilityConversionSpace::Screen].origin;

        rect = FloatRect(element.points[1], FloatSize());
        CGPoint newPoint2 = [wrapper convertRectToSpace:rect space:AccessibilityConversionSpace::Screen].origin;
        CGPathAddQuadCurveToPoint(newPath, nil, newPoint1.x, newPoint1.y, newPoint2.x, newPoint2.y);
        break;
    }
    case PathElement::Type::AddCurveToPoint:
    {
        rect = FloatRect(element.points[0], FloatSize());
        CGPoint newPoint1 = [wrapper convertRectToSpace:rect space:AccessibilityConversionSpace::Screen].origin;

        rect = FloatRect(element.points[1], FloatSize());
        CGPoint newPoint2 = [wrapper convertRectToSpace:rect space:AccessibilityConversionSpace::Screen].origin;

        rect = FloatRect(element.points[2], FloatSize());
        CGPoint newPoint3 = [wrapper convertRectToSpace:rect space:AccessibilityConversionSpace::Screen].origin;
        CGPathAddCurveToPoint(newPath, nil, newPoint1.x, newPoint1.y, newPoint2.x, newPoint2.y, newPoint3.x, newPoint3.y);
        break;
    }
    case PathElement::Type::CloseSubpath:
    {
        CGPathCloseSubpath(newPath);
        break;
    }
    }
}

- (CGPathRef)convertPathToScreenSpace:(const Path&)path
{
    auto convertedPath = adoptCF(CGPathCreateMutable());
    PathConversionInfo conversion = { self, convertedPath.get() };
    path.applyElements([&conversion](const PathElement& pathElement) {
        convertPathToScreenSpaceFunction(conversion, pathElement);
    });
    return convertedPath.autorelease();
}

// Determine the visible range by checking intersection of unobscuredContentRect and a range of text by
// advancing forward by line from top and backwards by line from the bottom, until we have a visible range.
- (NSRange)accessibilityVisibleCharacterRange
{

#if ENABLE(AX_THREAD_TEXT_APIS)
    if (AXObjectCache::useAXThreadTextApis()) {
        RefPtr<AXCoreObject> backingObject = self.baseUpdateBackingStore;
        if (!backingObject)
            return NSMakeRange(NSNotFound, 0);
        std::optional range = backingObject->visibleCharacterRange();
        return range ? *range : NSMakeRange(NSNotFound, 0);
    }
#endif // ENABLE(AX_THREAD_TEXT_APIS)

    return Accessibility::retrieveValueFromMainThread<NSRange>([protectedSelf = retainPtr(self)] () -> NSRange {
        RefPtr<AXCoreObject> backingObject = protectedSelf.get().baseUpdateBackingStore;
        if (!backingObject)
            return NSMakeRange(NSNotFound, 0);

        auto elementRange = makeNSRange(backingObject->simpleRange());
        if (elementRange.location == NSNotFound)
            return elementRange;

        std::optional visibleRange = backingObject->visibleCharacterRange();
        if (!visibleRange || visibleRange->location == NSNotFound)
            return NSMakeRange(NSNotFound, 0);

        return NSMakeRange(visibleRange->location - elementRange.location, visibleRange->length);
    });
}

- (id)_accessibilityWebDocumentView
{
    ASSERT_NOT_REACHED();
    // Overridden by sub-classes
    return nil;
}

- (CGRect)convertRectToSpace:(const WebCore::FloatRect&)rect space:(AccessibilityConversionSpace)space
{
    RefPtr<AXCoreObject> backingObject = self.baseUpdateBackingStore;
    if (!backingObject)
        return CGRectZero;

    return backingObject->convertRectToPlatformSpace(rect, space);
}

NSRange makeNSRange(std::optional<SimpleRange> range)
{
    if (!range)
        return NSMakeRange(NSNotFound, 0);
    
    Ref document = range->start.document();
    RefPtr frame = document->frame();
    if (!frame)
        return NSMakeRange(NSNotFound, 0);

    RefPtr rootEditableElement = frame->selection().selection().rootEditableElement();
    RefPtr scope = rootEditableElement ? rootEditableElement : document->documentElement();
    if (!scope)
        return NSMakeRange(NSNotFound, 0);

    // Mouse events may cause TSM to attempt to create an NSRange for a portion of the view
    // that is not inside the current editable region. These checks ensure we don't produce
    // potentially invalid data when responding to such requests.
    if (!scope->contains(range->start.container.ptr()) || !scope->contains(range->end.container.ptr()))
        return NSMakeRange(NSNotFound, 0);

    return NSMakeRange(characterCount({ { *scope, 0 }, range->start }), characterCount(*range));
}

std::optional<SimpleRange> makeDOMRange(Document* document, NSRange range)
{
    if (range.location == NSNotFound)
        return std::nullopt;

    // our critical assumption is that we are only called by input methods that
    // concentrate on a given area containing the selection
    // We have to do this because of text fields and textareas. The DOM for those is not
    // directly in the document DOM, so serialization is problematic. Our solution is
    // to use the root editable element of the selection start as the positional base.
    // That fits with AppKit's idea of an input context.
    RefPtr selectionRoot = document->frame()->selection().selection().rootEditableElement();
    RefPtr scope = selectionRoot ? selectionRoot : document->documentElement();
    if (!scope)
        return std::nullopt;

    return resolveCharacterRange(makeRangeSelectingNodeContents(*scope), range);
}

- (WebCore::AXCoreObject*)baseUpdateBackingStore
{
#if PLATFORM(MAC)
    RefPtr<AXCoreObject> backingObject = self.updateObjectBackingStore;
    if (!backingObject)
        return nullptr;
#else
    if (![self _prepareAccessibilityCall])
        return nullptr;
    RefPtr<AXCoreObject> backingObject = self.axBackingObject;
#endif
    return backingObject.get();
}

- (NSArray<NSDictionary *> *)lineRectsAndText
{
    ASSERT(isMainThread());

    RefPtr backingObject = dynamicDowncast<AccessibilityObject>(self.baseUpdateBackingStore);
    if (!backingObject)
        return nil;

    auto range = backingObject->simpleRange();
    if (!range)
        return nil;

    Vector<std::pair<IntRect, RetainPtr<NSAttributedString>>> lines;
    auto start = VisiblePosition { makeContainerOffsetPosition(range->start) };
    auto rangeEnd = VisiblePosition { makeContainerOffsetPosition(range->end) };
    while (!start.isNull() && start <= rangeEnd) {
        auto end = backingObject->nextLineEndPosition(start);
        if (end <= start)
            break;

        auto rect = backingObject->boundsForVisiblePositionRange({start, end});

        auto lineRange = makeSimpleRange(start, end);
        if (!lineRange)
            break;

        auto content = backingObject->contentForRange(*lineRange, AXCoreObject::SpellCheck::Yes);
        auto text = adoptNS([[NSMutableAttributedString alloc] init]);
        for (id item in content.get()) {
            if ([item isKindOfClass:NSAttributedString.class])
                [text appendAttributedString:item];
            else if ([item isKindOfClass:WebAccessibilityObjectWrapper.class]) {
#if PLATFORM(MAC)
                auto *wrapper = static_cast<WebAccessibilityObjectWrapper *>(item);
                RefPtr<AXCoreObject> object = wrapper.axBackingObject;
                if (!object)
                    continue;

                RetainPtr<NSString> label;
                switch (object->role()) {
                case AccessibilityRole::StaticText:
                    label = object->stringValue().createNSString();
                    break;
                case AccessibilityRole::Image: {
                    String name = object->titleAttributeValue();
                    if (name.isEmpty())
                        name = object->descriptionAttributeValue();
                    label = name.createNSString();
                    break;
                }
                default:
                    break;
                }
#else
                RetainPtr<NSString> label = static_cast<WebAccessibilityObjectWrapper *>(item).accessibilityLabel;
#endif
                if (!label)
                    continue;

                auto attributedLabel = adoptNS([[NSAttributedString alloc] initWithString:label.get()]);
                [text appendAttributedString:attributedLabel.get()];
            }
        }
        lines.append({rect, text});

        start = end;
        // If start is at a hard breakline "\n", move to the beginning of the next line.
        while (isEndOfLine(start)) {
            end = start.next();
            auto endOfLineRange = makeSimpleRange(start, end);
            if (!endOfLineRange)
                break;

            TextIterator it(*endOfLineRange);
            if (it.atEnd() || it.text().length() != 1 || it.text()[0] != '\n')
                break;

            start = end;
        }
    }

    if (lines.isEmpty())
        return nil;
    return createNSArray(lines, [self] (const auto& line) {
        return @{ @"rect": [NSValue valueWithRect:[self convertRectToSpace:FloatRect(line.first) space:AccessibilityConversionSpace::Screen]],
                  @"text": line.second.get() };
    }).autorelease();
}

- (NSString *)ariaLandmarkRoleDescription
{
    return self.axBackingObject->ariaLandmarkRoleDescription().createNSString().autorelease();
}

- (NSString *)accessibilityPlatformMathSubscriptKey
{
    ASSERT_NOT_REACHED();
    return nil;
}

- (NSString *)accessibilityPlatformMathSuperscriptKey
{
    ASSERT_NOT_REACHED();
    return nil;    
}

- (NSArray *)accessibilityMathPostscriptPairs
{
    AccessibilityObject::AccessibilityMathMultiscriptPairs pairs;
    self.axBackingObject->mathPostscripts(pairs);
    return convertMathPairsToNSArray(pairs, [self accessibilityPlatformMathSubscriptKey], [self accessibilityPlatformMathSuperscriptKey]);
}

- (NSArray *)accessibilityMathPrescriptPairs
{
    AccessibilityObject::AccessibilityMathMultiscriptPairs pairs;
    self.axBackingObject->mathPrescripts(pairs);
    return convertMathPairsToNSArray(pairs, [self accessibilityPlatformMathSubscriptKey], [self accessibilityPlatformMathSuperscriptKey]);
}

- (NSDictionary<NSString *, id> *)baseAccessibilityResolvedEditingStyles
{
    // We're only going to behave properly in this method if we're on the main-thread, since
    // that's the only time casting to AccessibilityObject is going to be successful.
    ASSERT(isMainThread());

    RefPtr axObject = dynamicDowncast<AccessibilityObject>(self.axBackingObject);
    if (!axObject)
        return nil;

    NSMutableDictionary<NSString *, id> *results = [NSMutableDictionary dictionary];
    auto editingStyles = axObject->resolvedEditingStyles();
    for (String& key : editingStyles.keys()) {
        auto value = editingStyles.get(key);
        RetainPtr result = WTF::switchOn(value,
            [] (String& typedValue) -> RetainPtr<id> { return typedValue.createNSString(); },
            [] (bool& typedValue) -> RetainPtr<id> { return @(typedValue); },
            [] (int& typedValue) -> RetainPtr<id> { return @(typedValue); },
            [] (auto&) { return nil; }
        );
        results[key.createNSString().get()] = result.get();
    }
    return results;
}

// This is set by DRT when it wants to listen for notifications.
static BOOL accessibilityShouldRepostNotifications;
+ (void)accessibilitySetShouldRepostNotifications:(BOOL)repost
{
    accessibilityShouldRepostNotifications = repost;
#if PLATFORM(MAC)
    AXObjectCache::setShouldRepostNotificationsForTests(repost);
#endif
}

- (void)accessibilityPostedNotification:(NSString *)notificationName
{
    if (accessibilityShouldRepostNotifications)
        [self accessibilityPostedNotification:notificationName userInfo:nil];
}

static bool isValueTypeSupported(id value)
{
#if PLATFORM(MAC)
    if (value && CFGetTypeID((__bridge CFTypeRef)value) == AXTextMarkerGetTypeID())
        return true;
#endif // PLATFORM(MAC)

    return [value isKindOfClass:[NSString class]] || [value isKindOfClass:[NSNumber class]] || [value isKindOfClass:[WebAccessibilityObjectWrapperBase class]];
}

static NSArray *arrayRemovingNonSupportedTypes(NSArray *array)
{
    ASSERT([array isKindOfClass:[NSArray class]]);
    auto mutableArray = adoptNS([array mutableCopy]);
    for (NSUInteger i = 0; i < [mutableArray count];) {
        id value = [mutableArray objectAtIndex:i];
        if ([value isKindOfClass:[NSDictionary class]])
            [mutableArray replaceObjectAtIndex:i withObject:dictionaryRemovingNonSupportedTypes(value)];
        else if ([value isKindOfClass:[NSArray class]])
            [mutableArray replaceObjectAtIndex:i withObject:arrayRemovingNonSupportedTypes(value)];
        else if (!isValueTypeSupported(value)) {
            [mutableArray removeObjectAtIndex:i];
            continue;
        }
        i++;
    }
    return mutableArray.autorelease();
}

static NSDictionary *dictionaryRemovingNonSupportedTypes(NSDictionary *dictionary)
{
    if (!dictionary)
        return nil;
    ASSERT([dictionary isKindOfClass:[NSDictionary class]]);
    auto mutableDictionary = adoptNS([dictionary mutableCopy]);
    for (NSString *key in dictionary) {
        id value = [dictionary objectForKey:key];
        if ([value isKindOfClass:[NSDictionary class]])
            [mutableDictionary setObject:dictionaryRemovingNonSupportedTypes(value) forKey:key];
        else if ([value isKindOfClass:[NSArray class]])
            [mutableDictionary setObject:arrayRemovingNonSupportedTypes(value) forKey:key];
        else if (!isValueTypeSupported(value))
            [mutableDictionary removeObjectForKey:key];
    }
    return mutableDictionary.autorelease();
}

- (void)accessibilityPostedNotification:(NSString *)notificationName userInfo:(NSDictionary *)userInfo
{
    if (accessibilityShouldRepostNotifications) {
        ASSERT(notificationName);
        userInfo = dictionaryRemovingNonSupportedTypes(userInfo);
        NSDictionary *info = [NSDictionary dictionaryWithObjectsAndKeys:notificationName, @"notificationName", userInfo, @"userInfo", nil];
        [[NSNotificationCenter defaultCenter] postNotificationName:NSAccessibilityDRTNotificationNotification object:self userInfo:info];
    }
}

- (NSString *)innerHTML
{
    if (RefPtr<AXCoreObject> backingObject = self.axBackingObject)
        return backingObject->innerHTML().createNSString().autorelease();
    return nil;
}

- (NSString *)outerHTML
{
    if (RefPtr<AXCoreObject> backingObject = self.axBackingObject)
        return backingObject->outerHTML().createNSString().autorelease();
    return nil;
}

#pragma mark Search helpers

using AccessibilitySearchKeyMap = HashMap<String, AccessibilitySearchKey>;

struct SearchKeyEntry {
    String key;
    AccessibilitySearchKey value;
};

static AccessibilitySearchKeyMap* createAccessibilitySearchKeyMap()
{
    const std::array searchKeys {
        SearchKeyEntry { NSAccessibilityAnyTypeSearchKey, AccessibilitySearchKey::AnyType },
        SearchKeyEntry { NSAccessibilityArticleSearchKey, AccessibilitySearchKey::Article },
        SearchKeyEntry { NSAccessibilityBlockquoteSameLevelSearchKey, AccessibilitySearchKey::BlockquoteSameLevel },
        SearchKeyEntry { NSAccessibilityBlockquoteSearchKey, AccessibilitySearchKey::Blockquote },
        SearchKeyEntry { NSAccessibilityBoldFontSearchKey, AccessibilitySearchKey::BoldFont },
        SearchKeyEntry { NSAccessibilityButtonSearchKey, AccessibilitySearchKey::Button },
        SearchKeyEntry { NSAccessibilityCheckBoxSearchKey, AccessibilitySearchKey::Checkbox },
        SearchKeyEntry { NSAccessibilityControlSearchKey, AccessibilitySearchKey::Control },
        SearchKeyEntry { NSAccessibilityDifferentTypeSearchKey, AccessibilitySearchKey::DifferentType },
        SearchKeyEntry { NSAccessibilityFontChangeSearchKey, AccessibilitySearchKey::FontChange },
        SearchKeyEntry { NSAccessibilityFontColorChangeSearchKey, AccessibilitySearchKey::FontColorChange },
        SearchKeyEntry { NSAccessibilityFrameSearchKey, AccessibilitySearchKey::Frame },
        SearchKeyEntry { NSAccessibilityGraphicSearchKey, AccessibilitySearchKey::Graphic },
        SearchKeyEntry { NSAccessibilityHeadingLevel1SearchKey, AccessibilitySearchKey::HeadingLevel1 },
        SearchKeyEntry { NSAccessibilityHeadingLevel2SearchKey, AccessibilitySearchKey::HeadingLevel2 },
        SearchKeyEntry { NSAccessibilityHeadingLevel3SearchKey, AccessibilitySearchKey::HeadingLevel3 },
        SearchKeyEntry { NSAccessibilityHeadingLevel4SearchKey, AccessibilitySearchKey::HeadingLevel4 },
        SearchKeyEntry { NSAccessibilityHeadingLevel5SearchKey, AccessibilitySearchKey::HeadingLevel5 },
        SearchKeyEntry { NSAccessibilityHeadingLevel6SearchKey, AccessibilitySearchKey::HeadingLevel6 },
        SearchKeyEntry { NSAccessibilityHeadingSameLevelSearchKey, AccessibilitySearchKey::HeadingSameLevel },
        SearchKeyEntry { NSAccessibilityHeadingSearchKey, AccessibilitySearchKey::Heading },
        SearchKeyEntry { NSAccessibilityHighlightedSearchKey, AccessibilitySearchKey::Highlighted },
        SearchKeyEntry { NSAccessibilityKeyboardFocusableSearchKey, AccessibilitySearchKey::KeyboardFocusable },
        SearchKeyEntry { NSAccessibilityItalicFontSearchKey, AccessibilitySearchKey::ItalicFont },
        SearchKeyEntry { NSAccessibilityLandmarkSearchKey, AccessibilitySearchKey::Landmark },
        SearchKeyEntry { NSAccessibilityLinkSearchKey, AccessibilitySearchKey::Link },
        SearchKeyEntry { NSAccessibilityListSearchKey, AccessibilitySearchKey::List },
        SearchKeyEntry { NSAccessibilityLiveRegionSearchKey, AccessibilitySearchKey::LiveRegion },
        SearchKeyEntry { NSAccessibilityMisspelledWordSearchKey, AccessibilitySearchKey::MisspelledWord },
        SearchKeyEntry { NSAccessibilityOutlineSearchKey, AccessibilitySearchKey::Outline },
        SearchKeyEntry { NSAccessibilityPlainTextSearchKey, AccessibilitySearchKey::PlainText },
        SearchKeyEntry { NSAccessibilityRadioGroupSearchKey, AccessibilitySearchKey::RadioGroup },
        SearchKeyEntry { NSAccessibilitySameTypeSearchKey, AccessibilitySearchKey::SameType },
        SearchKeyEntry { NSAccessibilityStaticTextSearchKey, AccessibilitySearchKey::StaticText },
        SearchKeyEntry { NSAccessibilityStyleChangeSearchKey, AccessibilitySearchKey::StyleChange },
        SearchKeyEntry { NSAccessibilityTableSameLevelSearchKey, AccessibilitySearchKey::TableSameLevel },
        SearchKeyEntry { NSAccessibilityTableSearchKey, AccessibilitySearchKey::Table },
        SearchKeyEntry { NSAccessibilityTextFieldSearchKey, AccessibilitySearchKey::TextField },
        SearchKeyEntry { NSAccessibilityUnderlineSearchKey, AccessibilitySearchKey::Underline },
        SearchKeyEntry { NSAccessibilityUnvisitedLinkSearchKey, AccessibilitySearchKey::UnvisitedLink },
        SearchKeyEntry { NSAccessibilityVisitedLinkSearchKey, AccessibilitySearchKey::VisitedLink }
    };
    
    AccessibilitySearchKeyMap* searchKeyMap = new AccessibilitySearchKeyMap;
    for (auto& searchKey : searchKeys)
        searchKeyMap->set(searchKey.key, searchKey.value);
    
    return searchKeyMap;
}

static AccessibilitySearchKey accessibilitySearchKeyForString(const String& value)
{
    if (value.isEmpty())
        return AccessibilitySearchKey::AnyType;
    
    static const AccessibilitySearchKeyMap* searchKeyMap = createAccessibilitySearchKeyMap();
    AccessibilitySearchKey searchKey = searchKeyMap->get(value);    
    return static_cast<int>(searchKey) ? searchKey : AccessibilitySearchKey::AnyType;
}

static std::optional<AccessibilitySearchKey> makeVectorElement(const AccessibilitySearchKey*, id arrayElement)
{
    if (![arrayElement isKindOfClass:NSString.class])
        return std::nullopt;
    return { { accessibilitySearchKeyForString(arrayElement) } };
}

AccessibilitySearchCriteria accessibilitySearchCriteriaForSearchPredicate(AXCoreObject& object, const NSDictionary *parameter)
{
    AccessibilitySearchCriteria criteria;
    criteria.anchorObject = &object;

    WebAccessibilityObjectWrapperBase *startElement = [parameter objectForKey:NSAccessibilitySearchCurrentElementKey];
    id startRange = [parameter objectForKey:NSAccessibilitySearchCurrentRangeKey];
    NSString *direction = [parameter objectForKey:NSAccessibilitySearchDirectionKey];
    NSNumber *immediateDescendantsOnly = [parameter objectForKey:NSAccessibilityImmediateDescendantsOnlyKey];
    NSNumber *resultsLimit = [parameter objectForKey:NSAccessibilitySearchResultsLimitKey];
    NSString *searchText = [parameter objectForKey:NSAccessibilitySearchTextKey];
    NSNumber *visibleOnly = [parameter objectForKey:NSAccessibilityVisibleOnlyKey];
    id searchKey = [parameter objectForKey:NSAccessibilitySearchIdentifiersKey];

    if ([startElement isKindOfClass:[WebAccessibilityObjectWrapperBase class]])
        criteria.startObject = startElement.axBackingObject;

    if ([startRange isKindOfClass:[NSValue class]] && nsValueHasObjCType<NSRange>((NSValue *)startRange))
        criteria.startRange = [(NSValue *)startRange rangeValue];

#if PLATFORM(MAC)
    else if (startRange && CFGetTypeID((__bridge CFTypeRef)startRange) == AXTextMarkerRangeGetTypeID()) {
        AXTextMarkerRange markerRange { (AXTextMarkerRangeRef)startRange };
        if (auto nsRange = markerRange.nsRange())
            criteria.startRange = *nsRange;

        if (!criteria.startObject)
            criteria.startObject = markerRange.start().object().get();
    }
#endif

    if ([direction isKindOfClass:[NSString class]])
        criteria.searchDirection = [direction isEqualToString:NSAccessibilitySearchDirectionNext] ? AccessibilitySearchDirection::Next : AccessibilitySearchDirection::Previous;

    if ([immediateDescendantsOnly isKindOfClass:[NSNumber class]])
        criteria.immediateDescendantsOnly = [immediateDescendantsOnly boolValue];

    if ([resultsLimit isKindOfClass:[NSNumber class]])
        criteria.resultsLimit = [resultsLimit unsignedIntValue];

    if ([searchText isKindOfClass:[NSString class]])
        criteria.searchText = searchText;

    if ([visibleOnly isKindOfClass:[NSNumber class]])
        criteria.visibleOnly = [visibleOnly boolValue];

    if ([searchKey isKindOfClass:[NSString class]])
        criteria.searchKeys.append(accessibilitySearchKeyForString(searchKey));
    else if ([searchKey isKindOfClass:[NSArray class]])
        criteria.searchKeys = makeVector<AccessibilitySearchKey>(searchKey);

    return criteria;
}

@end
