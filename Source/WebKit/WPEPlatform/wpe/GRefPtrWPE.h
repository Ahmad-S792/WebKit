/*
 * Copyright (C) 2025 Igalia S.L.
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

#pragma once

#include <wpe/WPEClipboard.h>
#include <wpe/WPEDRMDevice.h>
#include <wpe/WPEEvent.h>
#include <wtf/glib/GRefPtr.h>

namespace WTF {

template<> inline WPEEvent* refGPtr(WPEEvent* ptr)
{
    if (ptr) [[likely]]
        wpe_event_ref(ptr);
    return ptr;
}

template<> inline void derefGPtr(WPEEvent* ptr)
{
    if (ptr) [[likely]]
        wpe_event_unref(ptr);
}

template<> inline WPEClipboardContent* refGPtr(WPEClipboardContent* ptr)
{
    if (ptr) [[likely]]
        wpe_clipboard_content_ref(ptr);
    return ptr;
}

template<> inline void derefGPtr(WPEClipboardContent* ptr)
{
    if (ptr) [[likely]]
        wpe_clipboard_content_unref(ptr);
}

template<> inline WPEDRMDevice* refGPtr(WPEDRMDevice* ptr)
{
    if (ptr) [[likely]]
        wpe_drm_device_ref(ptr);
    return ptr;
}

template<> inline void derefGPtr(WPEDRMDevice* ptr)
{
    if (ptr) [[likely]]
        wpe_drm_device_unref(ptr);
}


} // namespace WTF
