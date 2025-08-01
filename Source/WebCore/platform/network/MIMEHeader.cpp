/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
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
#include "MIMEHeader.h"

#if ENABLE(MHTML)

#include "ParsedContentType.h"
#include "SharedBufferChunkReader.h"
#include <wtf/HashMap.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/StringHash.h>

namespace WebCore {

typedef HashMap<String, String> KeyValueMap;

static KeyValueMap retrieveKeyValuePairs(WebCore::SharedBufferChunkReader& buffer)
{
    KeyValueMap keyValuePairs;
    String line;
    String key;
    StringBuilder value;
    while (!(line = buffer.nextChunkAsUTF8StringWithLatin1Fallback()).isNull()) {
        if (line.isEmpty())
            break; // Empty line means end of key/value section.
        if (line[0] == '\t') {
            ASSERT(!key.isEmpty());
            value.append(StringView(line).substring(1));
            continue;
        }
        // New key/value, store the previous one if any.
        if (!key.isEmpty()) {
            if (keyValuePairs.find(key) != keyValuePairs.end())
                LOG_ERROR("Key duplicate found in MIME header. Key is '%s', previous value replaced.", key.ascii().data());
            keyValuePairs.add(key, value.toString().trim(deprecatedIsSpaceOrNewline));
            key = String();
            value.clear();
        }
        size_t semicolonIndex = line.find(':');
        if (semicolonIndex == notFound) {
            // This is not a key value pair, ignore.
            continue;
        }
        key = StringView(line).left(semicolonIndex).trim(isUnicodeCompatibleASCIIWhitespace<char16_t>).convertToASCIILowercase();
        value.append(StringView(line).substring(semicolonIndex + 1));
    }
    // Store the last property if there is one.
    if (!key.isEmpty())
        keyValuePairs.set(key, value.toString().trim(deprecatedIsSpaceOrNewline));
    return keyValuePairs;
}

RefPtr<MIMEHeader> MIMEHeader::parseHeader(SharedBufferChunkReader& buffer)
{
    auto mimeHeader = adoptRef(*new MIMEHeader);
    KeyValueMap keyValuePairs = retrieveKeyValuePairs(buffer);
    KeyValueMap::iterator mimeParametersIterator = keyValuePairs.find("content-type"_s);
    if (mimeParametersIterator != keyValuePairs.end()) {
        String contentType, charset, multipartType, endOfPartBoundary;
        if (auto parsedContentType = ParsedContentType::create(mimeParametersIterator->value)) {
            contentType = parsedContentType->mimeType();
            charset = parsedContentType->charset().trim(deprecatedIsSpaceOrNewline);
            multipartType = parsedContentType->parameterValueForName("type"_s);
            endOfPartBoundary = parsedContentType->parameterValueForName("boundary"_s);
        }
        mimeHeader->m_contentType = contentType;
        if (!mimeHeader->isMultipart())
            mimeHeader->m_charset = charset;
        else {
            mimeHeader->m_multipartType = multipartType;
            mimeHeader->m_endOfPartBoundary = endOfPartBoundary;
            if (mimeHeader->m_endOfPartBoundary.isNull()) {
                LOG_ERROR("No boundary found in multipart MIME header.");
                return nullptr;
            }
            mimeHeader->m_endOfPartBoundary = makeString("--"_s, mimeHeader->m_endOfPartBoundary);
            mimeHeader->m_endOfDocumentBoundary = makeString(mimeHeader->m_endOfPartBoundary, "--"_s);
        }
    }

    mimeParametersIterator = keyValuePairs.find("content-transfer-encoding"_s);
    if (mimeParametersIterator != keyValuePairs.end())
        mimeHeader->m_contentTransferEncoding = parseContentTransferEncoding(mimeParametersIterator->value);

    mimeParametersIterator = keyValuePairs.find("content-location"_s);
    if (mimeParametersIterator != keyValuePairs.end())
        mimeHeader->m_contentLocation = mimeParametersIterator->value;

    return mimeHeader;
}

MIMEHeader::Encoding MIMEHeader::parseContentTransferEncoding(StringView text)
{
    auto encoding = text.trim(isUnicodeCompatibleASCIIWhitespace<char16_t>);
    if (equalLettersIgnoringASCIICase(encoding, "base64"_s))
        return Base64;
    if (equalLettersIgnoringASCIICase(encoding, "quoted-printable"_s))
        return QuotedPrintable;
    if (equalLettersIgnoringASCIICase(encoding, "7bit"_s))
        return SevenBit;
    if (equalLettersIgnoringASCIICase(encoding, "binary"_s))
        return Binary;
    LOG_ERROR("Unknown encoding '%s' found in MIME header.", text.utf8().data());
    return Unknown;
}

MIMEHeader::MIMEHeader()
    : m_contentTransferEncoding(Unknown)
{
}

}

#endif
