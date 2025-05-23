/*
 *  Copyright (C) 2014 Igalia S.L.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#if USE(GLIB)

#include <gio/gio.h>
#include <utility>
#include <wtf/Noncopyable.h>

namespace WTF {

template<typename T>
struct GPtrDeleter {
    void operator()(T* ptr) const { g_free(ptr); }
};

template<typename T>
struct GFreeDeleter {
    void operator()(T* ptr) const { g_free(ptr); }
};

template<typename T, typename U = GPtrDeleter<T>>
using GUniquePtr = std::unique_ptr<T, U>;

#define FOR_EACH_GLIB_DELETER(macro) \
    macro(GError, g_error_free) \
    macro(GList, g_list_free) \
    macro(GSList, g_slist_free) \
    macro(GPatternSpec, g_pattern_spec_free) \
    macro(GDir, g_dir_close) \
    macro(GTimer, g_timer_destroy) \
    macro(GKeyFile, g_key_file_free) \
    macro(char*, g_strfreev) \
    macro(GVariantIter, g_variant_iter_free) \
    macro(GVariantType, g_variant_type_free) \
    macro(GMarkupParseContext, g_markup_parse_context_free)

#define WTF_DEFINE_GPTR_DELETER(typeName, deleterFunc) \
    template<> struct GPtrDeleter<typeName> \
    { \
        void operator() (typeName* ptr) const \
        { \
            deleterFunc(ptr); \
        } \
    };

FOR_EACH_GLIB_DELETER(WTF_DEFINE_GPTR_DELETER)
#undef FOR_EACH_GLIB_DELETER

#define WTF_DEFINE_DEPRECATED_GPTR_DELETER(typeName, deleterFunc) \
    template<> struct GPtrDeleter<typeName> { \
        void operator()(typeName* ptr) const \
        { \
            ALLOW_DEPRECATED_DECLARATIONS_BEGIN; \
            deleterFunc(ptr); \
            ALLOW_DEPRECATED_DECLARATIONS_END; \
        } \
    };

WTF_DEFINE_DEPRECATED_GPTR_DELETER(GValueArray, g_value_array_free)
#undef WTF_DEFINE_DEPRECATED_GPTR_DELETER

template <typename T> class GUniqueOutPtr {
    WTF_MAKE_NONCOPYABLE(GUniqueOutPtr);
public:
    GUniqueOutPtr()
        : m_ptr(nullptr)
    {
    }

    ~GUniqueOutPtr()
    {
        reset();
    }

    T*& outPtr()
    {
        reset();
        return m_ptr;
    }

    T* release()
    {
        return std::exchange(m_ptr, nullptr);
    }

    T& operator*() const
    {
        ASSERT(m_ptr);
        return *m_ptr;
    }

    T* operator->() const
    {
        ASSERT(m_ptr);
        return m_ptr;
    }

    T* get() const { return m_ptr; }

    bool operator!() const { return !m_ptr; }

    // This conversion operator allows implicit conversion to bool but not to other integer types.
    typedef T* GUniqueOutPtr::*UnspecifiedBoolType;
    operator UnspecifiedBoolType() const { return m_ptr ? &GUniqueOutPtr::m_ptr : 0; }

private:
    void reset()
    {
        if (m_ptr) {
            GUniquePtr<T> deletePtr(m_ptr);
            m_ptr = nullptr;
        }
    }

    T* m_ptr;
};

} // namespace WTF

using WTF::GUniquePtr;
using WTF::GUniqueOutPtr;
using WTF::GFreeDeleter;

#endif // USE(GLIB)
