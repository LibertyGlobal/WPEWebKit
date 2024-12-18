/*
 * Copyright (C) 2014 Igalia S.L.
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
#include <string>
#include "config.h"
#include "PlatformScreen.h"

#include "DestinationColorSpace.h"
#include "FloatRect.h"
#include "Logging.h"
#include "NotImplemented.h"
#include "Widget.h"

#include "host.hpp"
#include "videoDevice.hpp"
#include "manager.hpp"
#include "dsUtl.h"
#include "dsError.h"
#include "list.hpp"
#include "libIBus.h"
#include "videoOutputPort.hpp"
#include "videoOutputPortType.hpp"
#include "videoOutputPortConfig.hpp"

namespace WebCore {

int screenDepth(Widget*)
{
    notImplemented();
    return 24;
}

int screenDepthPerComponent(Widget*)
{
    notImplemented();
    return 8;
}

bool screenIsMonochrome(Widget*)
{
    notImplemented();
    return false;
}

bool screenHasInvertedColors()
{
    return false;
}

double screenDPI()
{
    notImplemented();
    return 96;
}

void setScreenDPIObserverHandler(Function<void()>&&, void*)
{
    notImplemented();
}

FloatRect screenRect(Widget* widget)
{
    // WPE can't offer any more useful information about the screen size,
    // so we use the Widget's bounds rectangle (size of which equals the WPE view size).

    if (!widget)
        return { };
    return widget->boundsRect();
}

FloatRect screenAvailableRect(Widget* widget)
{
    return screenRect(widget);
}

DestinationColorSpace screenColorSpace(Widget*)
{
    return DestinationColorSpace::SRGB();
}

bool screenSupportsExtendedColor(Widget*)
{
    return false;
}

bool GetHDRCapabilities()
{
    int stbCaps = 0;
    int tvCaps = 0;
    IARM_Result_t err = IARM_RESULT_SUCCESS;
    bool retValue = false;

    err = IARM_Bus_Init("wayland-egl-WPEWebProcess");
    if(IARM_RESULT_SUCCESS != err)
    {
        WTFLogAlways("Error initializing IARM.. error code : %d\n",err);
        return retValue;
    }

    err = IARM_Bus_Connect();
    if(IARM_RESULT_SUCCESS != err)
    {
        WTFLogAlways("Error connecting to IARM.. error code : %d\n",err);
        IARM_Bus_Term();
        return retValue;
    }

    device::Manager::Initialize();

    // Get STB HDR capabilities
    device::VideoDevice decoder = device::Host::getInstance().getVideoDevices().at(0);
    decoder.getHDRCapabilities(&stbCaps);
    WTFLogAlways("STB HDRCapabilities - [%d]", stbCaps);

    // Get TV HDR capabilities
    std::string strVideoPort = device::Host::getInstance().getDefaultVideoPortName();
    device::VideoOutputPort vPort = device::VideoOutputPortConfig::getInstance().getPort(strVideoPort.c_str());

    if(vPort.isDisplayConnected())
    {
        vPort.getTVHDRCapabilities(&tvCaps);
    }

    WTFLogAlways("TV HDRCapabilities - [%d]", tvCaps);

    if(stbCaps != 0 && tvCaps != 0)
    {
        retValue = true;
    }

    device::Manager::DeInitialize();
    IARM_Bus_Disconnect();
    IARM_Bus_Term();
    WTFLogAlways("GetHDRCapabilities : returning %s", retValue ? "true" : "false");
    return retValue;
}

bool screenSupportsHighDynamicRange(Widget* widget)
{
    if(!widget)
    {
        return false;
    }

    // Get HDR capabilities of TV and STB
    return GetHDRCapabilities();
}

#if ENABLE(TOUCH_EVENTS)
bool screenHasTouchDevice()
{
    return true;
}

bool screenIsTouchPrimaryInputDevice()
{
    return true;
}
#endif

} // namespace WebCore
