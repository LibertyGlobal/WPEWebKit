#pragma once

#include <string>

namespace Telemetry {

/*
* Helper function to get some telemetry data from Wayland.
*/
class IWaylandInfoGetter {
public:
    /*
     * Don't include:
     * #include <EGL/egl.h>
     * #include <EGL/eglplatform.h>
     * since there are import order issues.
     * Defining needed types as void*, like WebKit does.
     */
    typedef void *EGLConfig;
    typedef void *EGLContext;
    typedef void *EGLDisplay;
    typedef void *EGLSurface;

    virtual EGLDisplay getEGLDisplay() const = 0;
    virtual EGLConfig getEGLConfig() const = 0;
    virtual EGLSurface getEGLSurface() const = 0;
    virtual EGLContext getEGLContext() const = 0;
    virtual unsigned int getWindowWidth() const = 0;
    virtual unsigned int getWindowHeight() const = 0;
};

class IReport
{
public:
    enum class AVPipelineState {
        CREATE,
        PLAY,
        PAUSE,
        STOP,
        DESTROY,
        FIRST_FRAME_DECODED,
        END_OF_STREAM,
        DECRYPT_ERROR,
        PLAYBACK_ERROR,
        DRM_ERROR,
        ERROR,
        SEEK_START,
        SEEK_DONE,
        VIDEO_RESOLUTION_CHANGED,
        UNKNOWN
    };

    enum class MediaType {
        AUDIO,
        VIDEO,
        NONE
    };

    enum class DrmType {
        PLAYREADY,
        WIDEVINE,
        NONE,
        UNKNOWN
    };

    enum class WaylandAction
    {
        INIT_GFX,
        DEINIT_GFX,
        INIT_INPUTS,
        DEINIT_INPUTS
    };

    enum class WaylandGraphicsState
    {
        GFX_NOT_INITIALIZED,
        GFX_INITIALIZED
    };

    enum class WaylandInputsState
    {
        INPUTS_NOT_INITIALIZED,
        INPUTS_INITIALIZED
    };

    virtual ~IReport() = default;
    virtual void reportPlaybackState(
                    AVPipelineState state,
                    const std::string &additionalInfo,
                    MediaType mediaType) = 0;
    virtual void reportDrmInfo(
                    DrmType drmType,
                    const std::string &additionalInfo) = 0;
    virtual void reportWaylandInfo(
                    const IWaylandInfoGetter &getter,
                    WaylandAction action,
                    WaylandGraphicsState gfxState,
                    WaylandInputsState inputsState) = 0;
};
}
