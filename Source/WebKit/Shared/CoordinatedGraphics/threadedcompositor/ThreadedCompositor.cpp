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

#include "config.h"
#include "ThreadedCompositor.h"

#if USE(COORDINATED_GRAPHICS)

#include "CompositingRunLoop.h"
#include "EventDispatcher.h"
#include "ThreadedDisplayRefreshMonitor.h"
#include "WebProcess.h"
#include <WebCore/PlatformDisplay.h>
#include <WebCore/TransformationMatrix.h>
#include <wtf/SetForScope.h>

#if USE(LIBEPOXY)
#include <epoxy/gl.h>
#elif USE(OPENGL_ES)
#include <GLES2/gl2.h>
#else
#include <GL/gl.h>
#endif

namespace WebKit {
using namespace WebCore;

Ref<ThreadedCompositor> ThreadedCompositor::create(Client& client, ThreadedDisplayRefreshMonitor::Client& displayRefreshMonitorClient, PlatformDisplayID displayID, const IntSize& viewportSize, float scaleFactor, TextureMapper::PaintFlags paintFlags, bool nonCompositedWebGLEnabled)
{
    return adoptRef(*new ThreadedCompositor(client, displayRefreshMonitorClient, displayID, viewportSize, scaleFactor, paintFlags, nonCompositedWebGLEnabled));
}

ThreadedCompositor::ThreadedCompositor(Client& client, ThreadedDisplayRefreshMonitor::Client& displayRefreshMonitorClient, PlatformDisplayID displayID, const IntSize& viewportSize, float scaleFactor, TextureMapper::PaintFlags paintFlags, bool nonCompositedWebGLEnabled)
    : m_client(client)
    , m_paintFlags(paintFlags)
    , m_nonCompositedWebGLEnabled(nonCompositedWebGLEnabled)
    , m_compositingRunLoop(makeUnique<CompositingRunLoop>([this] { renderLayerTree(); }))
    , m_displayRefreshMonitor(ThreadedDisplayRefreshMonitor::create(displayID, displayRefreshMonitorClient))
{
    {
        // Locking isn't really necessary here, but it's done for consistency.
        Locker locker { m_attributes.lock };
        m_attributes.viewportSize = viewportSize;
        m_attributes.scaleFactor = scaleFactor;
        m_attributes.needsResize = !viewportSize.isEmpty();
    }

    m_compositingRunLoop->performTaskSync([this, protectedThis = Ref { *this }] {
        m_scene = adoptRef(new CoordinatedGraphicsScene(this));
        m_nativeSurfaceHandle = m_client.nativeSurfaceHandleForCompositing();

        createGLContext();
        if (m_context) {
            if (!m_nativeSurfaceHandle)
                m_paintFlags |= TextureMapper::PaintingMirrored;
            m_scene->setActive(true);
        }
    });
}

ThreadedCompositor::~ThreadedCompositor()
{
}

void ThreadedCompositor::createGLContext()
{
    ASSERT(!RunLoop::isMain());

    // If nonCompositedWebGL is enabled there will be a gl context created for the window to render WebGL. We can't
    // create another context for the same window.
    if (m_nonCompositedWebGLEnabled)
        return;

    // GLNativeWindowType depends on the EGL implementation: reinterpret_cast works
    // for pointers (only if they are 64-bit wide and not for other cases), and static_cast for
    // numeric types (and when needed they get extended to 64-bit) but not for pointers. Using
    // a plain C cast expression in this one instance works in all cases.
    static_assert(sizeof(GLNativeWindowType) <= sizeof(uint64_t), "GLNativeWindowType must not be longer than 64 bits.");
    auto windowType = (GLNativeWindowType) m_nativeSurfaceHandle;
    m_context = GLContext::createContextForWindow(windowType, &PlatformDisplay::sharedDisplayForCompositing());
    if (m_context)
        m_context->makeContextCurrent();
}

void ThreadedCompositor::invalidate()
{
    m_scene->detach();
    m_compositingRunLoop->stopUpdates();
    m_displayRefreshMonitor->invalidate();
    m_compositingRunLoop->performTaskSync([this, protectedThis = Ref { *this }] {
        if (m_context) {
            if (!m_context->makeContextCurrent())
                return;

            // Update the scene at this point ensures the layers state are correctly propagated
            // in the ThreadedCompositor and in the CompositingCoordinator.
            updateSceneWithoutRendering();

            m_scene->purgeGLResources();
            m_context = nullptr;
        }
        m_client.didDestroyGLContext();
        m_scene = nullptr;
    });
    m_compositingRunLoop = nullptr;
}

void ThreadedCompositor::suspend()
{
    if (++m_suspendedCount > 1)
        return;

    m_compositingRunLoop->suspend();
    m_compositingRunLoop->performTaskSync([this, protectedThis = Ref { *this }] {
        m_scene->setActive(false);
    });
}

void ThreadedCompositor::suspendToTransparent()
{
    // If we're in nonCompositedWebGL mode, the WebGLRenderingContext will have painted the
    // transparent background. We don't need to do anything besides suspending.
    if (m_nonCompositedWebGLEnabled) {
        suspend();
        return;
    }

    // When not in nonCompositedWebGL, we need to request a redraw to paint the transparent
    // background, and when the scene is completed, suspend.
    if (++m_suspendedCount > 1)
        return;

    // Set the flag for transparent and request a redraw.
    m_compositingRunLoop->performTaskSync([this, protectedThis = Ref { *this }] {
        m_suspendToTransparentState = SuspendToTransparentState::Requested;
    });
    m_compositingRunLoop->scheduleUpdate();
}

void ThreadedCompositor::resume()
{
    ASSERT(m_suspendedCount > 0);
    if (--m_suspendedCount > 0)
        return;

    m_compositingRunLoop->performTaskSync([this, protectedThis = Ref { *this }] {
        m_scene->setActive(true);
        m_suspendToTransparentState = SuspendToTransparentState::None;
    });
#if ENABLE(RESIZE_WAYLAND_SURFACES_ON_SUSPEND_PAINTING)
    if (!m_nonCompositedWebGLEnabled) {
        // need to resize the view on resume
        Locker locker { m_attributes.lock };
        m_attributes.needsResize = true;
    }
#endif

    m_compositingRunLoop->resume();
    m_compositingRunLoop->scheduleUpdate();
}

void ThreadedCompositor::setScaleFactor(float scale)
{
    Locker locker { m_attributes.lock };
    m_attributes.scaleFactor = scale;
    m_compositingRunLoop->scheduleUpdate();
}

void ThreadedCompositor::setScrollPosition(const IntPoint& scrollPosition, float scale)
{
    Locker locker { m_attributes.lock };
    m_attributes.scrollPosition = scrollPosition;
    m_attributes.scaleFactor = scale;
    m_compositingRunLoop->scheduleUpdate();
}

void ThreadedCompositor::setViewportSize(const IntSize& viewportSize, float scale)
{
    Locker locker { m_attributes.lock };
    m_attributes.viewportSize = viewportSize;
    m_attributes.scaleFactor = scale;
    m_attributes.needsResize = true;
    m_compositingRunLoop->scheduleUpdate();
}

void ThreadedCompositor::updateViewport()
{
    m_compositingRunLoop->scheduleUpdate();
}

void ThreadedCompositor::forceRepaint()
{
    // FIXME: Enable this for WPE once it's possible to do these forced updates
    // in a way that doesn't starve out the underlying graphics buffers.
#if PLATFORM(GTK) && !USE(WPE_RENDERER)
    m_compositingRunLoop->performTaskSync([this, protectedThis = Ref { *this }] {
        renderLayerTree();
    });
#endif
}

void ThreadedCompositor::renderNonCompositedWebGL()
{
    m_client.willRenderFrame();

    // Retrieve the scene attributes in a thread-safe manner.
    // Do this in order to free the structures memory, as they are not really used in this case.
    Vector<WebCore::CoordinatedGraphicsState> states;

    {
        LockHolder locker(m_attributes.lock);
        states = WTFMove(m_attributes.states);

        // Set clientRenderNextFrame to true so frameComplete() causes a call to renderNextFrame().
        m_attributes.clientRendersNextFrame = true;
    }

    m_scene->applyStateChangesAndNotifyVideoPosition(states);

    m_client.didRenderFrame();
}

void ThreadedCompositor::renderLayerTree()
{
    if (m_nonCompositedWebGLEnabled) {
        renderNonCompositedWebGL();
        return;
    }

    if (!m_scene || !m_scene->isActive())
        return;

    if (!m_context || !m_context->makeContextCurrent())
        return;

    // Retrieve the scene attributes in a thread-safe manner.
    WebCore::IntSize viewportSize;
    WebCore::IntPoint scrollPosition;
    float scaleFactor;
    bool needsResize;

    Vector<WebCore::CoordinatedGraphicsState> states;

    {
        Locker locker { m_attributes.lock };
        viewportSize = m_attributes.viewportSize;
        scrollPosition = m_attributes.scrollPosition;
        scaleFactor = m_attributes.scaleFactor;
        needsResize = m_attributes.needsResize;

        states = WTFMove(m_attributes.states);

        if (!states.isEmpty()) {
            // Client has to be notified upon finishing this scene update.
            m_attributes.clientRendersNextFrame = true;
        }

        // Reset the needsResize attribute to false.
        m_attributes.needsResize = false;
    }

    TransformationMatrix viewportTransform;
    viewportTransform.scale(scaleFactor);
    viewportTransform.translate(-scrollPosition.x(), -scrollPosition.y());

    // Resize the client, if necessary, before the will-render-frame call is dispatched.
    // GL viewport is updated separately, if necessary. This establishes sequencing where
    // everything inside the will-render and did-render scope is done for a constant-sized scene,
    // and similarly all GL operations are done inside that specific scope.
#if !ENABLE(RESIZE_WAYLAND_SURFACES_ON_SUSPEND_PAINTING)
    if (needsResize)
        m_client.resize(viewportSize);
#else
    // since we got this far, we know m_nonCompositedWebGLEnabled is false, no need to check
    if (needsResize && m_suspendToTransparentState != SuspendToTransparentState::Requested) {
        m_client.resize(viewportSize);
    } else if (m_suspendToTransparentState == SuspendToTransparentState::Requested) {
        // resizing the surfaces, to conserve the memory; going too small (like 1x1) will not properly work
        // on some platformns, so choosing 16x16 (should only take around 3*1kB in suspended, instead of eg. 3*8MB for HD)
        constexpr IntSize suspendedSize(16, 16);
        m_client.resize(suspendedSize);
    }
#endif

    m_client.willRenderFrame();

    if (needsResize)
        glViewport(0, 0, viewportSize.width(), viewportSize.height());

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    m_scene->applyStateChanges(states);
    if (m_suspendToTransparentState != SuspendToTransparentState::Requested)
        m_scene->paintToCurrentGLContext(viewportTransform, FloatRect { FloatPoint { }, viewportSize }, m_paintFlags);
    else
        m_suspendToTransparentState = SuspendToTransparentState::WaitingForFrameComplete;

    m_context->swapBuffers();

#if ENABLE(RESIZE_WAYLAND_SURFACES_ON_SUSPEND_PAINTING)
    // since we got this far, we know m_nonCompositedWebGLEnabled is false, no need to check
    if (m_suspendToTransparentState == SuspendToTransparentState::WaitingForFrameComplete) {
        /*  With triple buffering we normally have 3 screen buffers. The backing surfaces may only be really
            resized when they're presented - so we need 3 buffer swaps to make sure all these surfaces
            are resized to the requested suspendedSize, thus saving the memory in suspend
            (one swapBuffers is already done above) */
        for (int i=0; i<2; ++i) {
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            m_context->swapBuffers();
        }
    }
#endif

    if (m_scene->isActive())
        m_client.didRenderFrame();
}

void ThreadedCompositor::sceneUpdateFinished()
{
    // The composition has finished. Now we have to determine how to manage
    // the scene update completion.

    // The DisplayRefreshMonitor will be used to dispatch a callback on the client thread if:
    //  - clientRendersNextFrame is true (i.e. client has to be notified about the finished update), or
    //  - a DisplayRefreshMonitor callback was requested from the Web engine
    bool shouldDispatchDisplayRefreshCallback { false };

    {
        Locker locker { m_attributes.lock };
        shouldDispatchDisplayRefreshCallback = m_attributes.clientRendersNextFrame
            || m_displayRefreshMonitor->requiresDisplayRefreshCallback();
    }

    if (m_suspendToTransparentState == SuspendToTransparentState::WaitingForFrameComplete) {
        m_compositingRunLoop->suspend();
        m_scene->setActive(false);
        m_suspendToTransparentState = SuspendToTransparentState::None;
    }

    Locker stateLocker { m_compositingRunLoop->stateLock() };

    // Schedule the DisplayRefreshMonitor callback, if necessary.
    if (shouldDispatchDisplayRefreshCallback)
        m_displayRefreshMonitor->dispatchDisplayRefreshCallback();

    // Always notify the ScrollingTrees to make sure scrolling does not depend on the main thread.
    WebProcess::singleton().eventDispatcher().notifyScrollingTreesDisplayWasRefreshed(m_displayRefreshMonitor->displayID());

    // Mark the scene update as completed.
    m_compositingRunLoop->updateCompleted(stateLocker);
}

void ThreadedCompositor::updateSceneState(const CoordinatedGraphicsState& state)
{
    Locker locker { m_attributes.lock };
    m_attributes.states.append(state);
    m_compositingRunLoop->scheduleUpdate();
}

void ThreadedCompositor::updateScene()
{
    m_compositingRunLoop->scheduleUpdate();
}

void ThreadedCompositor::updateSceneWithoutRendering()
{
    Vector<WebCore::CoordinatedGraphicsState> states;

    {
        Locker locker { m_attributes.lock };
        states = WTFMove(m_attributes.states);

    }
    m_scene->applyStateChanges(states);
    m_scene->updateSceneState();
}

WebCore::DisplayRefreshMonitor& ThreadedCompositor::displayRefreshMonitor() const
{
    return m_displayRefreshMonitor.get();
}

void ThreadedCompositor::frameComplete()
{
    ASSERT(!RunLoop::isMain());
    sceneUpdateFinished();
}

void ThreadedCompositor::targetRefreshRateDidChange(unsigned rate)
{
    ASSERT(RunLoop::isMain());
    m_displayRefreshMonitor->setTargetRefreshRate(rate);
}

}
#endif // USE(COORDINATED_GRAPHICS)
