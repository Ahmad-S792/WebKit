/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if PLATFORM(IOS_FAMILY) && ENABLE(DEVICE_ORIENTATION)

#include <wtf/RefCounted.h>

namespace WebCore {

class MotionManagerClient;

class DeviceOrientationUpdateProvider : public RefCounted<DeviceOrientationUpdateProvider> {
public:
    virtual ~DeviceOrientationUpdateProvider() { }

    virtual void startUpdatingDeviceOrientation(MotionManagerClient&, const SecurityOriginData&) = 0;
    virtual void stopUpdatingDeviceOrientation(MotionManagerClient&) = 0;

    virtual void startUpdatingDeviceMotion(MotionManagerClient&, const SecurityOriginData&) = 0;
    virtual void stopUpdatingDeviceMotion(MotionManagerClient&) = 0;

    virtual void deviceOrientationChanged(double, double, double, double, double) = 0;
    virtual void deviceMotionChanged(double, double, double, double, double, double, std::optional<double>, std::optional<double>, std::optional<double>) = 0;
    
protected:
    DeviceOrientationUpdateProvider() = default;
};

} // namespace WebCore

#endif // PLATFORM(IOS_FAMILY) && ENABLE(DEVICE_ORIENTATION)
