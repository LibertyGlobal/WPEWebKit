/*
 * Copyright (C) 2017 Igalia S.L.
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

#include "config.h"

#include "WebDriverService.h"
#include <wtf/MainThread.h>
#include <wtf/Threading.h>

int main(int argc, char** argv)
{
    WebDriver::WebDriverService::platformInit();

    WTF::initializeMainThread();

    WebDriver::WebDriverService service;
    return service.run(argc, argv);
}

#if OS(WINDOWS)
extern "C" __declspec(dllexport) int WINAPI dllLauncherEntryPoint(int argc, const char* argv[])
{
    return main(argc, const_cast<char**>(argv));
}
#endif

/*
 * ONEM-33809: Added dummy eglGetConfigAttrib() function which is needed by odhott library.
 * This is dragged to WebDriver through WTF library which links libodhott.
 * This function should be deleted in the future when libodhott will be taken out of WTF.
 */
extern "C" __attribute__((visibility("default"))) unsigned int eglGetConfigAttrib(void *eglDisplay, void *eglConfig, int32_t attribute, int32_t *value)
{
    return 0;
}

extern "C" __attribute__((visibility("default"))) unsigned int eglQueryContext(void *eglDisplay, void *eglCtx, int32_t attribute, int32_t *value)
{
    return 0;
}

extern "C" __attribute__((visibility("default"))) const char* eglQueryString(void *eglDisplay, int32_t name)
{
    return "";
}

extern "C" __attribute__((visibility("default"))) unsigned int eglQuerySurface(void *eglDisplay, void *eglSurface, int32_t attribute, int32_t *value)
{
    return 0;
}
