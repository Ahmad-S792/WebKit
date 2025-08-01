/*
 * Copyright (C) 2007-2022 Apple Inc. All rights reserved.
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
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "FontSelectionAlgorithm.h"
#include "FontTaggedSettings.h"
#include "RenderStyleConstants.h"
#include "Settings.h"
#include "TextFlags.h"
#include <memory>
#include <wtf/AbstractRefCountedAndCanMakeWeakPtr.h>
#include <wtf/Forward.h>
#include <wtf/HashSet.h>
#include <wtf/RefCountedAndCanMakeWeakPtr.h>
#include <wtf/WeakHashSet.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class CSSFontFaceClient;
class CSSFontFaceSource;
class CSSFontSelector;
class CSSPrimitiveValue;
class CSSValue;
class CSSValueList;
class Document;
class Font;
class FontDescription;
class FontFace;
class FontFeatureValues;
class FontPaletteValues;
class MutableStyleProperties;
class ScriptExecutionContext;
class StyleProperties;
class StyleRuleFontFace;

enum class ExternalResourceDownloadPolicy : bool;

DECLARE_ALLOCATOR_WITH_HEAP_IDENTIFIER(CSSFontFace);
class CSSFontFace final : public RefCountedAndCanMakeWeakPtr<CSSFontFace> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(CSSFontFace, CSSFontFace);
public:
    static Ref<CSSFontFace> create(CSSFontSelector&, StyleRuleFontFace* cssConnection = nullptr, FontFace* wrapper = nullptr, bool isLocalFallback = false);
    virtual ~CSSFontFace();

    void setFamily(CSSValue&);
    void setStyle(CSSValue&);
    void setWeight(CSSValue&);
    void setWidth(CSSValue&);
    void setSizeAdjust(CSSValue&);
    void setUnicodeRange(CSSValueList&);
    void setFeatureSettings(CSSValue&);
    void setDisplay(CSSValue&);

    String family() const;
    String style() const;
    String weight() const;
    String width() const;
    String unicodeRange() const;
    String featureSettings() const;
    String display() const;
    String sizeAdjust() const;

    // Pending => Loading  => TimedOut
    //              ||  \\    //  ||
    //              ||   \\  //   ||
    //              ||    \\//    ||
    //              ||     //     ||
    //              ||    //\\    ||
    //              ||   //  \\   ||
    //              \/  \/    \/  \/
    //             Success    Failure
    enum class Status : uint8_t { Pending, Loading, TimedOut, Success, Failure };

    struct UnicodeRange {
        char32_t from;
        char32_t to;

        bool operator==(const UnicodeRange&) const = default;
    };

    std::span<const UnicodeRange> ranges() const LIFETIME_BOUND { ASSERT(m_status != Status::Failure); return m_ranges.span(); }

    RefPtr<CSSValue> familyCSSValue() const;

    void setFontSelectionCapabilities(FontSelectionCapabilities capabilities) { m_fontSelectionCapabilities = capabilities; }
    FontSelectionCapabilities fontSelectionCapabilities() const { ASSERT(m_status != Status::Failure); return m_fontSelectionCapabilities.computeFontSelectionCapabilities(); }

    bool isLocalFallback() const { return m_isLocalFallback; }
    Status status() const { return m_status; }
    StyleRuleFontFace* cssConnection() const;

    void addClient(CSSFontFaceClient&);
    void removeClient(CSSFontFaceClient&);

    bool computeFailureState() const;

    void opportunisticallyStartFontDataURLLoading();

    void adoptSource(std::unique_ptr<CSSFontFaceSource>&&);
    void sourcesPopulated() { m_sourcesPopulated = true; }
    size_t sourceCount() const { return m_sources.size(); }

    void fontLoaded(CSSFontFaceSource&);

    void load();

    RefPtr<Font> font(const FontDescription&, bool syntheticBold, bool syntheticItalic, ExternalResourceDownloadPolicy, const FontPaletteValues&, RefPtr<FontFeatureValues>);

    static void appendSources(CSSFontFace&, CSSValueList&, ScriptExecutionContext*, bool isInitiatingElementInUserAgentShadowTree);

    bool rangesMatchCodePoint(char32_t) const;

    // We don't guarantee that the FontFace wrapper will be the same every time you ask for it.
    Ref<FontFace> wrapper(ScriptExecutionContext*);
    void setWrapper(FontFace&);
    FontFace* existingWrapper();

    struct FontLoadTiming {
        Seconds blockPeriod;
        Seconds swapPeriod;
    };
    FontLoadTiming fontLoadTiming() const;
    bool shouldIgnoreFontLoadCompletions() const { return m_shouldIgnoreFontLoadCompletions; }

    bool purgeable() const;

    AllowUserInstalledFonts allowUserInstalledFonts() const { return m_allowUserInstalledFonts; }

    void updateStyleIfNeeded();

    bool hasSVGFontFaceSource() const;
    void setErrorState();

private:
    CSSFontFace(const SettingsValues*, StyleRuleFontFace*, FontFace*, bool isLocalFallback);

    size_t pump(ExternalResourceDownloadPolicy);
    void setStatus(Status);
    void notifyClientsOfFontPropertyChange();

    void initializeWrapper();

    void fontLoadEventOccurred();
    void timeoutFired();

    const StyleProperties& properties() const;
    MutableStyleProperties& mutableProperties();

    RefPtr<Document> protectedDocument();

    const Variant<Ref<MutableStyleProperties>, Ref<StyleRuleFontFace>> m_propertiesOrCSSConnection;
    RefPtr<CSSValue> m_family;
    Vector<UnicodeRange> m_ranges;

    FontFeatureSettings m_featureSettings;
    FontLoadingBehavior m_loadingBehavior { FontLoadingBehavior::Auto };

    float m_sizeAdjust { 1.0 };

    Vector<std::unique_ptr<CSSFontFaceSource>, 0, CrashOnOverflow, 0> m_sources;
    WeakHashSet<CSSFontFaceClient> m_clients;
    WeakPtr<FontFace> m_wrapper;
    FontSelectionSpecifiedCapabilities m_fontSelectionCapabilities;
    
    Status m_status { Status::Pending };
    bool m_isLocalFallback : 1 { false };
    bool m_sourcesPopulated : 1 { false };
    bool m_mayBePurged : 1 { true };
    bool m_shouldIgnoreFontLoadCompletions : 1 { false };
    FontLoadTimingOverride m_fontLoadTimingOverride { FontLoadTimingOverride::None };
    AllowUserInstalledFonts m_allowUserInstalledFonts { AllowUserInstalledFonts::Yes };

    Timer m_timeoutTimer;
};

class CSSFontFaceClient : public AbstractRefCountedAndCanMakeWeakPtr<CSSFontFaceClient> {
public:
    virtual ~CSSFontFaceClient() = default;
    virtual void fontLoaded(CSSFontFace&) { }
    virtual void fontStateChanged(CSSFontFace&, CSSFontFace::Status /*oldState*/, CSSFontFace::Status /*newState*/) { }
    virtual void fontPropertyChanged(CSSFontFace&, CSSValue* /*oldFamily*/ = nullptr) { }
    virtual void updateStyleIfNeeded(CSSFontFace&) { }
};

}
