/*
 * Copyright (C) 2011, 2013 Google Inc. All rights reserved.
 * Copyright (C) 2014-2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebVTTTokenizer.h"

#if ENABLE(VIDEO)

#include "CSSTokenizerInputStream.h"
#include "HTMLEntityParser.h"
#include "MarkupTokenizerInlines.h"
#include <wtf/text/StringBuilder.h>
#include <wtf/unicode/CharacterNames.h>

namespace WebCore {

#define WEBVTT_ADVANCE_TO(stateName)                        \
    do {                                                    \
        ASSERT(!m_input.isEmpty());                         \
        m_preprocessor.advance(m_input);                    \
        character = m_preprocessor.nextInputCharacter();    \
        goto stateName;                                     \
    } while (false)
#define WEBVTT_SWITCH_TO(stateName)                         \
    do { \
        ASSERT(!m_input.isEmpty()); \
        m_preprocessor.peek(m_input); \
        character = m_preprocessor.nextInputCharacter(); \
        goto stateName; \
    } while (false)

static void addNewClass(StringBuilder& classes, const StringBuilder& newClass)
{
    if (!classes.isEmpty())
        classes.append(' ');
    classes.append(newClass);
}

inline bool emitToken(WebVTTToken& resultToken, const WebVTTToken& token)
{
    resultToken = token;
    return true;
}

inline bool advanceAndEmitToken(SegmentedString& source, WebVTTToken& resultToken, const WebVTTToken& token)
{
    source.advance();
    return emitToken(resultToken, token);
}

WebVTTTokenizer::WebVTTTokenizer(const String& input)
    : m_input(input)
    , m_preprocessor(*this)
{
    // Append an EOF marker and close the input "stream".
    ASSERT(!m_input.isClosed());
    m_input.append(span(kEndOfFileMarker));
    m_input.close();
}

static void ProcessEntity(SegmentedString& source, StringBuilder& result, char16_t additionalAllowedCharacter = 0)
{
    auto decoded = consumeHTMLEntity(source, additionalAllowedCharacter);
    if (decoded.failed() || decoded.notEnoughCharacters())
        result.append('&');
    else {
        for (auto character : decoded.span())
            result.append(character);
    }
}

bool WebVTTTokenizer::nextToken(WebVTTToken& token)
{
    if (m_input.isEmpty() || !m_preprocessor.peek(m_input))
        return false;

    char16_t character = m_preprocessor.nextInputCharacter();
    if (character == kEndOfFileMarker) {
        m_preprocessor.advance(m_input);
        return false;
    }

    StringBuilder buffer;
    StringBuilder result;
    StringBuilder classes;

// 4.8.10.13.4 WebVTT cue text tokenizer
DataState:
    if (character == '&') {
        WEBVTT_ADVANCE_TO(HTMLCharacterReferenceInDataState);
    } else if (character == '<') {
        if (result.isEmpty())
            WEBVTT_ADVANCE_TO(TagState);
        else {
            // We don't want to advance input or perform a state transition - just return a (new) token.
            // (On the next call to nextToken we will see '<' again, but take the other branch in this if instead.)
            return emitToken(token, WebVTTToken::StringToken(result.toString()));
        }
    } else if (character == kEndOfFileMarker)
        return advanceAndEmitToken(m_input, token, WebVTTToken::StringToken(result.toString()));
    else {
        result.append(character);
        WEBVTT_ADVANCE_TO(DataState);
    }

TagState:
    if (isTokenizerWhitespace(character)) {
        ASSERT(result.isEmpty());
        WEBVTT_ADVANCE_TO(StartTagAnnotationState);
    } else if (character == '.') {
        ASSERT(result.isEmpty());
        WEBVTT_ADVANCE_TO(StartTagClassState);
    } else if (character == '/') {
        WEBVTT_ADVANCE_TO(EndTagState);
    } else if (isASCIIDigit(character)) {
        result.append(character);
        WEBVTT_ADVANCE_TO(TimestampTagState);
    } else if (character == '>' || character == kEndOfFileMarker) {
        ASSERT(result.isEmpty());
        return advanceAndEmitToken(m_input, token, WebVTTToken::StartTag(result.toString()));
    } else {
        result.append(character);
        WEBVTT_ADVANCE_TO(StartTagState);
    }

StartTagState:
    if (isTokenizerWhitespace(character))
        WEBVTT_ADVANCE_TO(StartTagAnnotationState);
    else if (character == '.')
        WEBVTT_ADVANCE_TO(StartTagClassState);
    else if (character == '>' || character == kEndOfFileMarker)
        return advanceAndEmitToken(m_input, token, WebVTTToken::StartTag(result.toString()));
    else {
        result.append(character);
        WEBVTT_ADVANCE_TO(StartTagState);
    }

StartTagClassState:
    if (isTokenizerWhitespace(character)) {
        addNewClass(classes, buffer);
        buffer.clear();
        WEBVTT_ADVANCE_TO(StartTagAnnotationState);
    } else if (character == '.') {
        addNewClass(classes, buffer);
        buffer.clear();
        WEBVTT_ADVANCE_TO(StartTagClassState);
    } else if (character == '>' || character == kEndOfFileMarker) {
        addNewClass(classes, buffer);
        buffer.clear();
        return advanceAndEmitToken(m_input, token, WebVTTToken::StartTag(result.toString(), classes.toAtomString()));
    } else {
        buffer.append(character);
        WEBVTT_ADVANCE_TO(StartTagClassState);
    }

StartTagAnnotationState:
    if (character == '&')
        WEBVTT_ADVANCE_TO(HTMLCharacterReferenceInAnnotationState);
    else if (character == '>' || character == kEndOfFileMarker)
        return advanceAndEmitToken(m_input, token, WebVTTToken::StartTag(result.toString(), classes.toAtomString(), buffer.toAtomString()));
    buffer.append(character);
    WEBVTT_ADVANCE_TO(StartTagAnnotationState);

EndTagState:
    if (character == '>' || character == kEndOfFileMarker)
        return advanceAndEmitToken(m_input, token, WebVTTToken::EndTag(result.toString()));
    result.append(character);
    WEBVTT_ADVANCE_TO(EndTagState);

TimestampTagState:
    if (character == '>' || character == kEndOfFileMarker)
        return advanceAndEmitToken(m_input, token, WebVTTToken::TimestampTag(result.toString()));
    result.append(character);
    WEBVTT_ADVANCE_TO(TimestampTagState);

HTMLCharacterReferenceInDataState:
    ProcessEntity(m_input, result);
    WEBVTT_SWITCH_TO(DataState);

HTMLCharacterReferenceInAnnotationState:
    ProcessEntity(m_input, result, '>');
    WEBVTT_SWITCH_TO(StartTagAnnotationState);
}

}

#endif
