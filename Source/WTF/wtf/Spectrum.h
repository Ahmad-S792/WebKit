/*
 * Copyright (C) 2011, 2014 Apple Inc. All rights reserved.
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

#include <wtf/HashMap.h>
#include <wtf/Vector.h>
#include <algorithm>

namespace WTF {

template<typename T, typename CounterType = unsigned>
class Spectrum {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(Spectrum);
public:
    typedef typename HashMap<T, CounterType>::iterator iterator;
    typedef typename HashMap<T, CounterType>::const_iterator const_iterator;
    
    Spectrum() { }
    
    void add(const T& key, CounterType count = 1)
    {
        Locker locker(m_lock);
        if (!count)
            return;
        typename HashMap<T, CounterType>::AddResult result = m_map.add(key, count);
        if (!result.isNewEntry)
            result.iterator->value += count;
    }
    
    template<typename U>
    void addAll(const Spectrum<T, U>& otherSpectrum)
    {
        for (auto& entry : otherSpectrum)
            add(entry.key, entry.count);
    }
    
    CounterType get(const T& key) const
    {
        Locker locker(m_lock);
        const_iterator iter = m_map.find(key);
        if (iter == m_map.end())
            return 0;
        return iter->value;
    }
    
    size_t size() const { return m_map.size(); }
    
    const_iterator begin() const LIFETIME_BOUND { return m_map.begin(); }
    const_iterator end() const LIFETIME_BOUND { return m_map.end(); }
    
    struct KeyAndCount {
        WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(KeyAndCount);
        KeyAndCount() { }
        
        KeyAndCount(const T& key, CounterType count)
            : key(&key)
            , count(count)
        {
        }
        
        bool operator<(const KeyAndCount& other) const
        {
            if (count != other.count)
                return count < other.count;
            // This causes lower-ordered keys being returned first; this is really just
            // here to make sure that the order is somewhat deterministic rather than being
            // determined by hashing.
            return *key > *other.key;
        }

        const T* key;
        CounterType count;
    };

    Lock& getLock() { return m_lock; }
    
    // Returns a list ordered from lowest-count to highest-count.
    Vector<KeyAndCount> buildList(AbstractLocker&) const
    {
        Vector<KeyAndCount> list;
        for (const auto& entry : *this)
            list.append(KeyAndCount(entry.key, entry.value));
        
        std::sort(list.begin(), list.end());
        return list;
    }
    
    void clear()
    {
        Locker locker(m_lock);
        m_map.clear();
    }
    
    template<typename Functor>
    void removeIf(const Functor& functor)
    {
        Locker locker(m_lock);
        m_map.removeIf([&functor] (typename HashMap<T, CounterType>::KeyValuePairType& pair) {
                return functor(KeyAndCount(pair.key, pair.value));
            });
    }
    
private:
    mutable Lock m_lock;
    HashMap<T, CounterType> m_map;
};

} // namespace WTF

using WTF::Spectrum;
