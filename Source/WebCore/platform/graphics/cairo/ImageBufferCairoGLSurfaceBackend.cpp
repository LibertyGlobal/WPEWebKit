#include "config.h"
#include "ImageBufferCairoGLSurfaceBackend.h"

#if ENABLE(ACCELERATED_2D_CANVAS)

#include "GLContextEGL.h"
#include "PixelBuffer.h"
#include "TextureMapperPlatformLayerBuffer.h"
#include "TextureMapperPlatformLayerProxyGL.h"
#include <cairo-gl.h>
#include <epoxy/gl.h>
#include <mutex>
#include <wtf/IsoMallocInlines.h>

namespace WebCore {

WTF_MAKE_ISO_ALLOCATED_IMPL(ImageBufferCairoGLSurfaceBackend);

static cairo_device_t* cairoDevice()
{
    static cairo_device_t* s_device { nullptr };

    static std::once_flag s_flag;
    std::call_once(s_flag,
        [&] {
            auto& platformDisplay = PlatformDisplay::sharedDisplayForCompositing();
            auto* context = platformDisplay.sharingGLContext();
            if (is<GLContextEGL>(context))
                s_device = cairo_egl_device_create(platformDisplay.eglDisplay(), downcast<GLContextEGL>(context)->context());
        });

    return s_device;
}

static RefPtr<cairo_surface_t>
cairoGLSurfaceCopyToImageSurface(cairo_surface_t* surface)
{
    auto copy = adoptRef(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cairo_gl_surface_get_width(surface), cairo_gl_surface_get_height(surface)));

    auto cr = adoptRef(cairo_create(copy.get()));
    cairo_set_operator(cr.get(), CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr.get(), surface, 0, 0);
    cairo_paint(cr.get());

    return copy;
}

class ImageBufferCairoGLDisplayDelegate final : public GraphicsLayerContentsDisplayDelegate {
public:
    ImageBufferCairoGLDisplayDelegate(Ref<Nicosia::ContentLayer>&& nicosiaLayer)
        : m_nicosiaLayer(WTFMove(nicosiaLayer))
    { }

    PlatformLayer* platformLayer() const final { return m_nicosiaLayer.ptr(); }

private:
    Ref<Nicosia::ContentLayer> m_nicosiaLayer;
};

unsigned ImageBufferCairoGLSurfaceBackend::calculateBytesPerRow(const IntSize& backendSize)
{
    ASSERT(!backendSize.isEmpty());
    return cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, backendSize.width());
}

size_t ImageBufferCairoGLSurfaceBackend::calculateMemoryCost(const Parameters& parameters)
{
    IntSize backendSize = calculateBackendSize(parameters);
    return ImageBufferBackend::calculateMemoryCost(backendSize, calculateBytesPerRow(backendSize));
}

static inline void clearSurface(cairo_surface_t* surface)
{
    RefPtr<cairo_t> cr = adoptRef(cairo_create(surface));
    cairo_set_operator(cr.get(), CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr.get());
}

std::unique_ptr<ImageBufferCairoGLSurfaceBackend> ImageBufferCairoGLSurfaceBackend::create(const Parameters& parameters, const ImageBuffer::CreationContext&)
{
    IntSize backendSize = calculateBackendSize(parameters);
    if (backendSize.isEmpty())
        return { };

    auto* context = PlatformDisplay::sharedDisplayForCompositing().sharingGLContext();
    context->makeContextCurrent();

    std::array<uint32_t, 2> textures { 0, 0 };

    glGenTextures(1, &textures[0]);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, backendSize.width(), backendSize.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    auto* device = cairoDevice();
    if (!device) {
        glDeleteTextures(1, &textures[0]);
        return { };
    }
    cairo_gl_device_set_thread_aware(device, FALSE);

    std::array<RefPtr<cairo_surface_t>, 2> surfaces;
    surfaces[0] = adoptRef(cairo_gl_surface_create_for_texture(device, CAIRO_CONTENT_COLOR_ALPHA, textures[0], backendSize.width(), backendSize.height()));
    if (cairo_surface_status(surfaces[0].get()) != CAIRO_STATUS_SUCCESS) {
        glDeleteTextures(1, &textures[0]);
        return nullptr;
    }
    clearSurface(surfaces[0].get());

    return std::unique_ptr<ImageBufferCairoGLSurfaceBackend>(new ImageBufferCairoGLSurfaceBackend(parameters, textures, surfaces));
}

ImageBufferCairoGLSurfaceBackend::ImageBufferCairoGLSurfaceBackend(const Parameters& parameters, const std::array<uint32_t, 2>& textures, const std::array<RefPtr<cairo_surface_t>, 2>& surfaces)
    : ImageBufferCairoSurfaceBackend(parameters, RefPtr { surfaces[0] })
    , m_textures(textures)
    , m_surfaces(surfaces)
{
    m_nicosiaLayer = Nicosia::ContentLayer::create(Nicosia::ContentLayerTextureMapperImpl::createFactory(*this));
    m_layerContentsDisplayDelegate = adoptRef(new ImageBufferCairoGLDisplayDelegate(Ref { *m_nicosiaLayer}));
}

ImageBufferCairoGLSurfaceBackend::~ImageBufferCairoGLSurfaceBackend()
{
    downcast<Nicosia::ContentLayerTextureMapperImpl>(m_nicosiaLayer->impl()).invalidateClient();

    GLContext* previousActiveContext = GLContext::current();
    auto* context = PlatformDisplay::sharedDisplayForCompositing().sharingGLContext();
    context->makeContextCurrent();

    glDeleteTextures(1, &m_textures[0]);
    if (m_textures[1])
        glDeleteTextures(1, &m_textures[1]);

    if (previousActiveContext)
        previousActiveContext->makeContextCurrent();
}

RefPtr<GraphicsLayerContentsDisplayDelegate> ImageBufferCairoGLSurfaceBackend::layerContentsDisplayDelegate() const
{
    return m_layerContentsDisplayDelegate;
}

bool ImageBufferCairoGLSurfaceBackend::copyToPlatformTexture(GraphicsContextGL&, GCGLenum target, PlatformGLObject destinationTexture, GCGLenum internalFormat, bool premultiplyAlpha, bool flipY) const
{
    if (!premultiplyAlpha || flipY)
        return false;

    if (!m_textures[0])
        return false;

    GCGLenum bindTextureTarget;
    switch (target) {
    case GL_TEXTURE_2D:
        bindTextureTarget = GL_TEXTURE_2D;
        break;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        bindTextureTarget = GL_TEXTURE_CUBE_MAP;
        break;
    default:
        return false;
    }

    auto backendSize = this->backendSize();
    cairo_surface_flush(m_surfaces[0].get());

    std::unique_ptr<GLContext> context = GLContext::createOffscreenContext(&PlatformDisplay::sharedDisplayForCompositing());
    context->makeContextCurrent();
    uint32_t fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[0], 0);
    glBindTexture(bindTextureTarget, destinationTexture);
    glCopyTexImage2D(target, 0, internalFormat, 0, 0, backendSize.width(), backendSize.height(), 0);
    glBindTexture(bindTextureTarget, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFlush();
    glDeleteFramebuffers(1, &fbo);
    return true;
}

void ImageBufferCairoGLSurfaceBackend::swapBuffersIfNeeded()
{
    auto backendSize = this->backendSize();

    GLContext* previousActiveContext = GLContext::current();

    if (!m_textures[1]) {
        auto* context = PlatformDisplay::sharedDisplayForCompositing().sharingGLContext();
        context->makeContextCurrent();

        glGenTextures(1, &m_textures[1]);
        glBindTexture(GL_TEXTURE_2D, m_textures[1]);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, backendSize.width(), backendSize.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        m_surfaces[1] = adoptRef(cairo_gl_surface_create_for_texture(cairoDevice(), CAIRO_CONTENT_COLOR_ALPHA, m_textures[1], backendSize.width(), backendSize.height()));
        m_compositorContext = adoptRef(cairo_create(m_surfaces[1].get()));
    }

    // It would be great if we could just swap the buffers here as we do with webgl, but that breaks the cases
    // where one frame uses the content already rendered in the previous frame. So we just copy the content
    // into the compositor buffer.
    cairo_set_source_surface(m_compositorContext.get(), m_surfaces[0].get(), 0, 0);
    cairo_set_operator(m_compositorContext.get(), CAIRO_OPERATOR_SOURCE);
    cairo_paint(m_compositorContext.get());
    cairo_surface_flush(m_surfaces[1].get());
    glFlush();

    {
        auto& proxy = downcast<Nicosia::ContentLayerTextureMapperImpl>(m_nicosiaLayer->impl()).proxy();
        ASSERT(is<TextureMapperPlatformLayerProxyGL>(proxy));

        if (proxy.isEmpty()) {
            Locker locker { proxy.lock() };
            downcast<TextureMapperPlatformLayerProxyGL>(proxy).pushNextBuffer(makeUnique<TextureMapperPlatformLayerBuffer>(m_textures[1], backendSize, TextureMapperGL::ShouldBlend, GL_RGBA));
        }
    }

    if (previousActiveContext)
        previousActiveContext->makeContextCurrent();
}

unsigned ImageBufferCairoGLSurfaceBackend::bytesPerRow() const
{
    IntSize backendSize = calculateBackendSize(m_parameters);
    return calculateBytesPerRow(backendSize);
}

RefPtr<PixelBuffer> ImageBufferCairoGLSurfaceBackend::getPixelBuffer(const PixelBufferFormat& outputFormat, const IntRect& srcRect, const ImageBufferAllocator& allocator) const
{
    auto surface = cairoGLSurfaceCopyToImageSurface(m_surfaces[0].get());
    return ImageBufferBackend::getPixelBuffer(outputFormat, srcRect, cairo_image_surface_get_data(surface.get()), allocator);
}

void ImageBufferCairoGLSurfaceBackend::putPixelBuffer(const PixelBuffer& pixelBuffer, const IntRect& srcRect, const IntPoint& destPoint, AlphaPremultiplication destFormat)
{
    // Create a new image surface and put the data there. Then copy its contents to the original gl surface in the specified rect.
    auto surface = adoptRef(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cairo_gl_surface_get_width(m_surfaces[0].get()), cairo_gl_surface_get_height(m_surfaces[0].get())));
    IntRect srcRectScaled = toBackendCoordinates(srcRect);
    IntPoint destPointScaled = toBackendCoordinates(destPoint);
    ImageBufferBackend::putPixelBuffer(pixelBuffer, srcRect, destPoint, destFormat, cairo_image_surface_get_data(surface.get()));
    cairo_surface_mark_dirty_rectangle(surface.get(), destPointScaled.x(), destPointScaled.y(), srcRectScaled.width(), srcRectScaled.height());

    auto cr = adoptRef(cairo_create(m_surfaces[0].get()));
    cairo_set_operator(cr.get(), CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr.get(), surface.get(), destPointScaled.x(), destPointScaled.y());
    cairo_rectangle(cr.get(), destPointScaled.x(), destPointScaled.y(), srcRectScaled.width(), srcRectScaled.height());
    cairo_fill(cr.get());
}

IntSize ImageBufferCairoGLSurfaceBackend::backendSize() const
{
    return IntSize { cairo_gl_surface_get_width(m_surfaces[0].get()), cairo_gl_surface_get_height(m_surfaces[0].get()) };
}

RefPtr<NativeImage> ImageBufferCairoGLSurfaceBackend::copyNativeImage(BackingStoreCopy copyBehavior) const
{
    switch (copyBehavior) {
    case CopyBackingStore: {
        auto copy = cairoGLSurfaceCopyToImageSurface(m_surfaces[0].get());
        return NativeImage::create(WTFMove(copy));
    }
    case DontCopyBackingStore:
        return NativeImage::create(RefPtr { m_surface.get() });
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}


} // namespace WebCore

#endif // ENABLE(ACCELERATED_2D_CANVAS)
