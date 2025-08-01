/*
 * Copyright (C) 2011 Google Inc.
 * Copyright (C) 2017 Yusuke Suzuki <utatane.tea@gmail.com>
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
 * THIS SOFTWARE IS PROVIDED BY GOOGLE, INC. ``AS IS'' AND ANY
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

#include <wtf/Noncopyable.h>
#include <wtf/StdLibExtras.h>

namespace WTF {

class RandomDevice {
    WTF_MAKE_NONCOPYABLE(RandomDevice);
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(RandomDevice);
public:
#if OS(DARWIN) || OS(FUCHSIA) || OS(WINDOWS)
    RandomDevice() = default;
#else
    RandomDevice();
    ~RandomDevice();
#endif

    // This function attempts to fill buffer with randomness from the operating
    // system. Rather than calling this function directly, consider calling
    // cryptographicallyRandomNumber or cryptographicallyRandomValues.
    void cryptographicallyRandomValues(std::span<uint8_t> buffer);

private:
#if OS(DARWIN) || OS(FUCHSIA) || OS(WINDOWS)
#elif OS(UNIX)
    int m_fd { -1 };
#else
#error "This configuration doesn't have a strong source of randomness."
// WARNING: When adding new sources of OS randomness, the randomness must
//          be of cryptographic quality!
#endif
};

}
