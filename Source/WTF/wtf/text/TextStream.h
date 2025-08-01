/*
 * Copyright (C) 2004, 2008 Apple Inc. All rights reserved.
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

#include <span>
#include <wtf/Forward.h>
#include <wtf/HexNumber.h>
#include <wtf/Markable.h>
#include <wtf/OptionSet.h>
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/StringBuilder.h>

namespace WTF {

class TextStream {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(TextStream);
public:
    struct FormatNumberRespectingIntegers {
        WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(FormatNumberRespectingIntegers);
        FormatNumberRespectingIntegers(double number)
            : value(number) { }

        double value;
    };
    
    enum class Formatting : uint8_t {
        SVGStyleRect                = 1 << 0, // "at (0,0) size 10x10"
        NumberRespectingIntegers    = 1 << 1,
        LayoutUnitsAsIntegers       = 1 << 2,
    };

    enum class LineMode { SingleLine, MultipleLine };
    TextStream(LineMode lineMode = LineMode::MultipleLine, OptionSet<Formatting> formattingFlags = { }, unsigned containerSizeLimit = 0)
        : m_formattingFlags(formattingFlags)
        , m_multiLineMode(lineMode == LineMode::MultipleLine)
        , m_containerSizeLimit(containerSizeLimit)
    {
    }

    bool isEmpty() const { return m_text.isEmpty(); }

    WTF_EXPORT_PRIVATE TextStream& operator<<(bool);
    WTF_EXPORT_PRIVATE TextStream& operator<<(char);
    WTF_EXPORT_PRIVATE TextStream& operator<<(int);
    WTF_EXPORT_PRIVATE TextStream& operator<<(unsigned);
    WTF_EXPORT_PRIVATE TextStream& operator<<(long);
    WTF_EXPORT_PRIVATE TextStream& operator<<(unsigned long);
    WTF_EXPORT_PRIVATE TextStream& operator<<(long long);

    WTF_EXPORT_PRIVATE TextStream& operator<<(unsigned long long);
    WTF_EXPORT_PRIVATE TextStream& operator<<(float);
    WTF_EXPORT_PRIVATE TextStream& operator<<(double);
    WTF_EXPORT_PRIVATE TextStream& operator<<(const char*);
    WTF_EXPORT_PRIVATE TextStream& operator<<(const void*);
    WTF_EXPORT_PRIVATE TextStream& operator<<(const AtomString&);
    WTF_EXPORT_PRIVATE TextStream& operator<<(const CString&);
    WTF_EXPORT_PRIVATE TextStream& operator<<(const String&);
    WTF_EXPORT_PRIVATE TextStream& operator<<(ASCIILiteral);
    WTF_EXPORT_PRIVATE TextStream& operator<<(StringView);
    WTF_EXPORT_PRIVATE TextStream& operator<<(const HexNumberBuffer&);
    WTF_EXPORT_PRIVATE TextStream& operator<<(const FormattedCSSNumber&);
    // Deprecated. Use the NumberRespectingIntegers FormattingFlag instead.
    WTF_EXPORT_PRIVATE TextStream& operator<<(const FormatNumberRespectingIntegers&);

#if PLATFORM(COCOA)
    WTF_EXPORT_PRIVATE TextStream& operator<<(id);
#endif

    OptionSet<Formatting> formattingFlags() const { return m_formattingFlags; }
    void setFormattingFlags(OptionSet<Formatting> flags) { m_formattingFlags = flags; }

    bool hasFormattingFlag(Formatting flag) const { return m_formattingFlags.contains(flag); }

    template<typename T>
    void dumpProperty(const String& name, const T& value)
    {
        TextStream& ts = *this;
        ts.startGroup();
        ts << name << ' ' << value;
        ts.endGroup();
    }

    template<typename T>
    void dumpProperty(ASCIILiteral name, const T& value)
    {
        TextStream& ts = *this;
        ts.startGroup();
        ts << name << ' ' << value;
        ts.endGroup();
    }

    WTF_EXPORT_PRIVATE String release();
    
    WTF_EXPORT_PRIVATE void startGroup();
    WTF_EXPORT_PRIVATE void endGroup();
    WTF_EXPORT_PRIVATE void nextLine(); // Output newline and indent.

    int indent() const { return m_indent; }
    void setIndent(int indent) { m_indent = indent; }
    void increaseIndent(int amount = 1) { m_indent += amount; }
    void decreaseIndent(int amount = 1) { m_indent -= amount; ASSERT(m_indent >= 0); }

    WTF_EXPORT_PRIVATE void writeIndent();

    unsigned containerSizeLimit() const { return m_containerSizeLimit; }

    // Stream manipulators.
    TextStream& operator<<(TextStream& (*func)(TextStream&))
    {
        return (*func)(*this);
    }

    struct Repeat {
        WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(Repeat);
        Repeat(unsigned inWidth, char inCharacter)
            : width(inWidth), character(inCharacter)
        { }
        unsigned width { 0 };
        char character { ' ' };
    };

    TextStream& operator<<(const Repeat& repeated)
    {
        for (unsigned i = 0; i < repeated.width; ++i)
            m_text.append(repeated.character);

        return *this;
    }

    class IndentScope {
    public:
        IndentScope(TextStream& ts, int amount = 1)
            : m_stream(ts)
            , m_amount(amount)
        {
            m_stream.increaseIndent(m_amount);
        }
        ~IndentScope()
        {
            m_stream.decreaseIndent(m_amount);
        }

    private:
        TextStream& m_stream;
        int m_amount;
    };

    class GroupScope {
    public:
        GroupScope(TextStream& ts)
            : m_stream(ts)
        {
            m_stream.startGroup();
        }
        ~GroupScope()
        {
            m_stream.endGroup();
        }

    private:
        TextStream& m_stream;
    };

private:
    StringBuilder m_text;
    int m_indent { 0 };
    OptionSet<Formatting> m_formattingFlags;
    bool m_multiLineMode { true };
    unsigned m_containerSizeLimit { 0 };
};

inline TextStream& indent(TextStream& ts)
{
    ts.writeIndent();
    return ts;
}

template<typename T>
struct ValueOrNull {
    explicit ValueOrNull(T* inValue)
        : value(inValue)
    { }
    T* value;
};

template<typename T>
TextStream& operator<<(TextStream& ts, ValueOrNull<T> item)
{
    if (item.value)
        ts << *item.value;
    else
        ts << "null"_s;
    return ts;
}

template<typename Item>
TextStream& operator<<(TextStream& ts, const std::optional<Item>& item)
{
    if (item)
        return ts << item.value();
    
    return ts << "nullopt"_s;
}

template<typename T, typename Traits>
TextStream& operator<<(TextStream& ts, const Markable<T, Traits>& item)
{
    if (item)
        return ts << item.value();
    
    return ts << "unset"_s;
}

template<typename... Ts>
TextStream& operator<<(TextStream& ts, const Variant<Ts...>& variant)
{
    WTF::switchOn(variant, [&](const auto& alternative) { ts << alternative; });
    return ts;
}

template<typename SizedContainer>
TextStream& streamSizedContainer(TextStream& ts, const SizedContainer& sizedContainer)
{
    ts << '[';

    unsigned count = 0;
    for (const auto& value : sizedContainer) {
        if (count)
            ts << ", "_s;
        ts << value;
        if (++count == ts.containerSizeLimit())
            break;
    }

    if (count != sizedContainer.size())
        ts << ", ..."_s;

    return ts << ']';
}

template<typename ItemType, size_t inlineCapacity>
TextStream& operator<<(TextStream& ts, const Vector<ItemType, inlineCapacity>& vector)
{
    return streamSizedContainer(ts, vector);
}

template<typename ItemType>
TextStream& operator<<(TextStream& ts, const FixedVector<ItemType>& vector)
{
    return streamSizedContainer(ts, vector);
}

template<typename ValueArg, typename HashArg, typename TraitsArg, typename TableTraitsArg, ShouldValidateKey shouldValidateKey>
TextStream& operator<<(TextStream& ts, const HashSet<ValueArg, HashArg, TraitsArg, TableTraitsArg, shouldValidateKey>& set)
{
    return streamSizedContainer(ts, set);
}

template<typename T, typename U>
TextStream& operator<<(TextStream& ts, const ListHashSet<T, U>& set)
{
    return streamSizedContainer(ts, set);
}

template<typename T, size_t size>
TextStream& operator<<(TextStream& ts, const std::array<T, size>& array)
{
    return streamSizedContainer(ts, array);
}

template<typename T, size_t Extent>
TextStream& operator<<(TextStream& ts, std::span<T, Extent> items)
{
    return streamSizedContainer(ts, items);
}

template<typename T, typename Counter>
TextStream& operator<<(TextStream& ts, const WeakPtr<T, Counter>& item)
{
    if (item)
        return ts << *item;
    
    return ts << "null"_s;
}

template<typename T>
TextStream& operator<<(TextStream& ts, const RefPtr<T>& item)
{
    if (item)
        return ts << *item;
    
    return ts << "null"_s;
}

template<typename T>
TextStream& operator<<(TextStream& ts, const Ref<T>& item)
{
    return ts << item.get();
}

template<typename T>
TextStream& operator<<(TextStream& ts, const CheckedPtr<T>& item)
{
    if (item)
        return ts << *item;

    return ts << "null"_s;
}

template<typename KeyArg, typename MappedArg, typename HashArg, typename KeyTraitsArg, typename MappedTraitsArg, typename TableTraitsArg, ShouldValidateKey shouldValidateKey, typename Malloc>
TextStream& operator<<(TextStream& ts, const HashMap<KeyArg, MappedArg, HashArg, KeyTraitsArg, MappedTraitsArg, TableTraitsArg, shouldValidateKey, Malloc>& map)
{
    ts << '{';

    unsigned count = 0;
    for (const auto& keyValuePair : map) {
        if (count)
            ts << ", "_s;
        ts << keyValuePair.key << ": "_s << keyValuePair.value;
        if (++count == ts.containerSizeLimit())
            break;
    }

    if (count != map.size())
        ts << ", ..."_s;

    return ts << '}';
}

template<typename Option>
TextStream& operator<<(TextStream& ts, const OptionSet<Option>& options)
{
    ts << '[';
    bool needComma = false;
    for (auto option : options) {
        if (needComma)
            ts << ", "_s;
        needComma = true;
        ts << option;
    }
    return ts << ']';
}

template<typename T, typename U>
TextStream& operator<<(TextStream& ts, const std::pair<T, U>& pair)
{
    return ts << '[' << pair.first << ", "_s << pair.second << ']';
}

template<typename, typename = void, typename = void, typename = void, typename = void, size_t = 0>
struct supports_text_stream_insertion : std::false_type { };

template<typename T>
struct supports_text_stream_insertion<T, std::void_t<decltype(std::declval<TextStream&>() << std::declval<T>())>> : std::true_type { };

template<typename ItemType, size_t inlineCapacity>
struct supports_text_stream_insertion<Vector<ItemType, inlineCapacity>> : supports_text_stream_insertion<ItemType> { };

template<typename ItemType>
struct supports_text_stream_insertion<FixedVector<ItemType>> : supports_text_stream_insertion<ItemType> { };

template<typename ValueArg, typename HashArg, typename TraitsArg>
struct supports_text_stream_insertion<HashSet<ValueArg, HashArg, TraitsArg>> : supports_text_stream_insertion<ValueArg> { };

template<typename KeyArg, typename MappedArg, typename HashArg, typename KeyTraitsArg, typename MappedTraitsArg, typename TableTraitsArg, ShouldValidateKey shouldValidateKey, typename Malloc>
struct supports_text_stream_insertion<HashMap<KeyArg, MappedArg, HashArg, KeyTraitsArg, MappedTraitsArg, TableTraitsArg, shouldValidateKey, Malloc>> : std::conjunction<supports_text_stream_insertion<KeyArg>, supports_text_stream_insertion<MappedArg>> { };

template<typename T, typename Traits>
struct supports_text_stream_insertion<Markable<T, Traits>> : supports_text_stream_insertion<T> { };

template<typename T>
struct supports_text_stream_insertion<OptionSet<T>> : supports_text_stream_insertion<T> { };

template<typename... Ts>
struct supports_text_stream_insertion<Variant<Ts...>> : std::conjunction<supports_text_stream_insertion<Ts>...> { };

template<typename T>
struct supports_text_stream_insertion<std::optional<T>> : supports_text_stream_insertion<T> { };

template<typename T, typename Counter>
struct supports_text_stream_insertion<WeakPtr<T, Counter>> : supports_text_stream_insertion<T> { };

template<typename T>
struct supports_text_stream_insertion<RefPtr<T>> : supports_text_stream_insertion<T> { };

template<typename T>
struct supports_text_stream_insertion<Ref<T>> : supports_text_stream_insertion<T> { };

template<typename T>
struct supports_text_stream_insertion<std::unique_ptr<T>> : supports_text_stream_insertion<T> { };

template<typename T, size_t size>
struct supports_text_stream_insertion<std::array<T, size>> : supports_text_stream_insertion<T> { };

template<typename T, typename U>
struct supports_text_stream_insertion<std::pair<T, U>> : std::conjunction<supports_text_stream_insertion<T>, supports_text_stream_insertion<U>> { };

template<typename... Ts>
struct supports_text_stream_insertion<std::tuple<Ts...>> : std::conjunction<supports_text_stream_insertion<Ts>...> { };

template<typename T>
struct ValueOrEllipsis {
    explicit ValueOrEllipsis(const T& value)
        : value(value)
    { }
    const T& value;
};

template<typename T>
TextStream& operator<<(TextStream& ts, ValueOrEllipsis<T> item)
{
    if constexpr (supports_text_stream_insertion<T>::value)
        ts << item.value;
    else
        ts << "..."_s;
    return ts;
}

// Deprecated. Use TextStream::writeIndent() instead.
WTF_EXPORT_PRIVATE void writeIndent(TextStream&, int indent);

} // namespace WTF

using WTF::TextStream;
using WTF::ValueOrEllipsis;
using WTF::ValueOrNull;
using WTF::indent;
