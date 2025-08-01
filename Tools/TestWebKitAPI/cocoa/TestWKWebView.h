/*
 * Copyright (C) 2016-2020 Apple Inc. All rights reserved.
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

#import <WebKit/WebKit.h>
#import <wtf/Forward.h>
#import <wtf/IterationStatus.h>
#import <wtf/RetainPtr.h>

@class _WKFrameTreeNode;
@class _WKProcessPoolConfiguration;

#if PLATFORM(IOS_FAMILY)
#import "WKBrowserEngineDefinitions.h"
@class _WKActivatedElementInfo;
@class _WKTextInputContext;
@class UITextSuggestion;
@class UIWKDocumentContext;
@class UIWKDocumentRequest;
@protocol UITextInputInternal;
@protocol UITextInputMultiDocument;
@protocol UITextInputPrivate;
@protocol UITextInputTraits_Private;
@protocol UIWKInteractionViewProtocol_Staging_95652872;
@protocol BETextInput;
@protocol BEExtendedTextInputTraits;
#endif

@interface WKWebView (AdditionalDeclarations)
#if PLATFORM(MAC)
- (void)paste:(id)sender;
- (void)changeAttributes:(id)sender;
- (void)changeColor:(id)sender;
- (void)superscript:(id)sender;
- (void)subscript:(id)sender;
- (void)unscript:(id)sender;
#endif
@end

namespace TestWebKitAPI {

struct AutocorrectionContext {
    String contextBeforeSelection;
    String selectedText;
    String contextAfterSelection;
    String markedText;
    NSRange selectedRangeInMarkedText;
};

} // namespace TestWebKitAPI

namespace WebCore {
class Color;
}

@interface WKWebView (TestWebKitAPI)
#if PLATFORM(IOS_FAMILY)
@property (nonatomic, readonly) CGRect selectionClipRect;
@property (nonatomic, readonly) BOOL hasAsyncTextInput;
#if USE(BROWSERENGINEKIT)
@property (nonatomic, readonly) id<BETextInput> asyncTextInput;
@property (nonatomic, readonly) id<BEExtendedTextInputTraits> extendedTextInputTraits;
#endif
#if HAVE(UI_WK_DOCUMENT_CONTEXT)
- (void)synchronouslyAdjustSelectionWithDelta:(NSRange)range;
#endif
@property (nonatomic, readonly) UIView <UITextInputPrivate, UITextInputInternal, UITextInputMultiDocument, UIWKInteractionViewProtocol_Staging_95652872, UITextInputTokenizer> *textInputContentView;
@property (nonatomic, readonly) TestWebKitAPI::AutocorrectionContext autocorrectionContext;
@property (nonatomic, readonly) id<UITextInputTraits_Private> effectiveTextInputTraits;
- (std::pair<CGRect, CGRect>)autocorrectionRectsForString:(NSString *)string;
- (NSArray<_WKTextInputContext *> *)synchronouslyRequestTextInputContextsInRect:(CGRect)rect;
- (void)replaceText:(NSString *)input withText:(NSString *)correction shouldUnderline:(BOOL)shouldUnderline completion:(void(^)())completion;
- (void)insertText:(NSString *)primaryString alternatives:(NSArray<NSString *> *)alternatives;
- (void)handleKeyEvent:(WebEvent *)event completion:(void (^)(WebEvent *theEvent, BOOL handled))completion;
- (void)selectTextForContextMenuWithLocationInView:(CGPoint)locationInView completion:(void(^)(BOOL shouldPresent))completion;
- (void)selectTextInGranularity:(UITextGranularity)granularity atPoint:(CGPoint)locationInView;
- (void)defineSelection;
- (void)shareSelection;
- (void)moveSelectionToStartOfParagraph;
- (void)extendSelectionToStartOfParagraph;
- (void)moveSelectionToEndOfParagraph;
- (void)extendSelectionToEndOfParagraph;
- (void)insertTextSuggestion:(UITextSuggestion *)textSuggestion;
#if HAVE(UI_WK_DOCUMENT_CONTEXT)
- (UIWKDocumentContext *)synchronouslyRequestDocumentContext:(UIWKDocumentRequest *)request;
#endif
#endif // PLATFORM(IOS_FAMILY)

- (CALayer *)firstLayerWithName:(NSString *)layerName;
- (void)forEachCALayer:(IterationStatus(^)(CALayer *))visitor;

@property (nonatomic, readonly) CGImageRef snapshotAfterScreenUpdates;
@property (nonatomic, readonly) NSUInteger gpuToWebProcessConnectionCount;
@property (nonatomic, readonly) NSUInteger modelProcessModelPlayerCount;
@property (nonatomic, readonly) NSString *contentsAsString;
@property (nonatomic, readonly) NSData *contentsAsWebArchive;
@property (nonatomic, readonly) NSArray<NSString *> *tagsInBody;
@property (nonatomic, readonly) NSString *selectedText;
- (void)loadTestPageNamed:(NSString *)pageName;
- (void)synchronouslyGoBack;
- (void)synchronouslyGoForward;
- (void)synchronouslyLoadHTMLString:(NSString *)html;
- (void)synchronouslyLoadHTMLString:(NSString *)html baseURL:(NSURL *)url;
- (void)synchronouslyLoadHTMLString:(NSString *)html preferences:(WKWebpagePreferences *)preferences;
- (void)synchronouslyLoadRequest:(NSURLRequest *)request;
- (void)synchronouslyLoadSimulatedRequest:(NSURLRequest *)request responseHTMLString:(NSString *)htmlString;
- (void)synchronouslyLoadRequest:(NSURLRequest *)request preferences:(WKWebpagePreferences *)preferences;
- (void)synchronouslyLoadRequestIgnoringSSLErrors:(NSURLRequest *)request;
- (void)synchronouslyLoadTestPageNamed:(NSString *)pageName;
- (void)synchronouslyLoadTestPageNamed:(NSString *)pageName asStringWithBaseURL:(NSURL *)url;
- (void)synchronouslyLoadTestPageNamed:(NSString *)pageName preferences:(WKWebpagePreferences *)preferences;
- (BOOL)_synchronouslyExecuteEditCommand:(NSString *)command argument:(NSString *)argument;
- (void)expectElementTagsInOrder:(NSArray<NSString *> *)tagNames;
- (void)expectElementCount:(NSInteger)count querySelector:(NSString *)querySelector;
- (void)expectElementTag:(NSString *)tagName toComeBefore:(NSString *)otherTagName;
- (BOOL)evaluateMediaQuery:(NSString *)query;
- (NSString *)stringByEvaluatingJavaScript:(NSString *)script;
- (NSString *)stringByEvaluatingJavaScript:(NSString *)script inFrame:(WKFrameInfo *)frame;
- (id)objectByEvaluatingJavaScriptWithUserGesture:(NSString *)script;
- (id)objectByEvaluatingJavaScript:(NSString *)script;
- (id)objectByEvaluatingJavaScript:(NSString *)script inFrame:(WKFrameInfo *)frame;
- (id)objectByCallingAsyncFunction:(NSString *)script withArguments:(NSDictionary *)arguments error:(NSError **)errorOut;
- (unsigned)waitUntilClientWidthIs:(unsigned)expectedClientWidth;
- (CGRect)elementRectFromSelector:(NSString *)selector;
- (CGPoint)elementMidpointFromSelector:(NSString *)selector;
@end

@interface TestMessageHandler : NSObject <WKScriptMessageHandler>
- (void)addMessage:(NSString *)message withHandler:(dispatch_block_t)handler;
@property (nonatomic, copy) void (^didReceiveScriptMessage)(NSString *);
@end

@interface TestWKWebView : WKWebView
- (instancetype)initWithFrame:(CGRect)frame configuration:(WKWebViewConfiguration *)configuration processPoolConfiguration:(_WKProcessPoolConfiguration *)processPoolConfiguration;
- (instancetype)initWithFrame:(CGRect)frame configuration:(WKWebViewConfiguration *)configuration addToWindow:(BOOL)addToWindow;
- (void)synchronouslyLoadHTMLStringAndWaitUntilAllImmediateChildFramesPaint:(NSString *)html;
- (void)clearMessageHandlers:(NSArray *)messageNames;
- (void)performAfterReceivingMessage:(NSString *)message action:(dispatch_block_t)action;
- (void)performAfterReceivingAnyMessage:(void (^)(NSString *))action;
- (void)waitForMessage:(NSString *)message;
- (void)waitForMessages:(NSArray<NSString *> *)messages;

// This function waits until a DOM load event is fired.
// FIXME: Rename this function to better describe what "after loading" means.
- (void)performAfterLoading:(dispatch_block_t)actions;

- (void)waitForNextPresentationUpdate;
- (void)waitForNextVisibleContentRectUpdate;
- (void)waitUntilActivityStateUpdateDone;
- (void)forceDarkMode;
- (NSString *)stylePropertyAtSelectionStart:(NSString *)propertyName;
- (NSString *)stylePropertyAtSelectionEnd:(NSString *)propertyName;
- (void)collapseToStart;
- (void)collapseToEnd;
- (void)addToTestWindow;
- (void)removeFromTestWindow;
- (BOOL)selectionRangeHasStartOffset:(int)start endOffset:(int)end;
- (BOOL)selectionRangeHasStartOffset:(int)start endOffset:(int)end inFrame:(WKFrameInfo *)frameInfo;
- (void)clickOnElementID:(NSString *)elementID;
- (void)waitForPendingMouseEvents;
- (void)focus;
- (std::optional<CGPoint>)getElementMidpoint:(NSString *)selector;
- (Vector<WebCore::Color>)sampleColors;
- (Vector<WebCore::Color>)sampleColorsWithInterval:(unsigned)interval;
- (RetainPtr<_WKFrameTreeNode>)frameTree;
@end

#if PLATFORM(IOS_FAMILY)
@interface UIView (WKTestingUIViewUtilities)
- (UIView *)wkFirstSubviewWithClass:(Class)targetClass;
- (UIView *)wkFirstSubviewWithBoundsSize:(CGSize)size;
@end
#endif

#if PLATFORM(IOS_FAMILY)
@interface WKContentView : UIView
@end

@interface TestWKWebView (IOSOnly)
@property (nonatomic) UIEdgeInsets overrideSafeAreaInset;
@property (nonatomic, readonly) CGRect caretViewRectInContentCoordinates;
@property (nonatomic, readonly) NSArray<NSValue *> *selectionViewRectsInContentCoordinates;
@property (nonatomic, readonly) NSString *textForSpeakSelection;
- (_WKActivatedElementInfo *)activatedElementAtPosition:(CGPoint)position;
- (void)evaluateJavaScriptAndWaitForInputSessionToChange:(NSString *)script;
- (WKContentView *)wkContentView;
- (void)setZoomScaleSimulatingUserTriggeredZoom:(CGFloat)zoomScale;
@end
#endif

#if PLATFORM(MAC)
@interface TestWKWebView (MacOnly)
// Simulates clicking with a pressure-sensitive device, if possible.
- (void)mouseDownAtPoint:(NSPoint)pointInWindow simulatePressure:(BOOL)simulatePressure;
- (void)mouseDownAtPoint:(NSPoint)pointInWindow simulatePressure:(BOOL)simulatePressure withFlags:(NSEventModifierFlags)flags eventType:(NSEventType)eventType;
- (void)mouseDragToPoint:(NSPoint)pointInWindow;
- (void)mouseEnterAtPoint:(NSPoint)pointInWindow;
- (void)mouseUpAtPoint:(NSPoint)pointInWindow;
- (void)mouseUpAtPoint:(NSPoint)pointInWindow withFlags:(NSEventModifierFlags)flags eventType:(NSEventType)eventType;
- (void)mouseMoveToPoint:(NSPoint)pointInWindow withFlags:(NSEventModifierFlags)flags;
- (void)sendClicksAtPoint:(NSPoint)pointInWindow numberOfClicks:(NSUInteger)numberOfClicks;
- (void)sendClickAtPoint:(NSPoint)pointInWindow;
- (void)rightClickAtPoint:(NSPoint)pointInWindow;
- (void)wheelEventAtPoint:(CGPoint)pointInWindow wheelDelta:(CGSize)delta;
- (BOOL)acceptsFirstMouseAtPoint:(NSPoint)pointInWindow;
- (NSWindow *)hostWindow;
- (void)typeCharacter:(char)character modifiers:(NSEventModifierFlags)modifiers;
- (void)typeCharacter:(char)character;
- (void)sendKey:(NSString *)characters code:(unsigned short)keyCode isDown:(BOOL)isDown modifiers:(NSEventModifierFlags)modifiers;
- (void)setEventTimestampOffset:(NSTimeInterval)offset;
@property (nonatomic, readonly) NSArray<NSString *> *collectLogsForNewConnections;
@property (nonatomic, readonly) NSTimeInterval eventTimestamp;
@property (nonatomic) BOOL forceWindowToBecomeKey;
@end
#endif

@interface TestWKWebView (SiteIsolation)
- (_WKFrameTreeNode *)mainFrame;
- (WKFrameInfo *)firstChildFrame;
- (WKFrameInfo *)secondChildFrame;
- (void)evaluateJavaScript:(NSString *)string inFrame:(WKFrameInfo *)frame completionHandler:(void(^)(id, NSError *))completionHandler;
- (WKFindResult *)findStringAndWait:(NSString *)string withConfiguration:(WKFindConfiguration *)configuration;
@end

