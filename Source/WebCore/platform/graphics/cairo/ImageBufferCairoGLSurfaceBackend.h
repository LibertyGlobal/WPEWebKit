#pragma once

#if ENABLE(ACCELERATED_2D_CANVAS)

#include "ImageBufferCairoSurfaceBackend.h"

#include "GraphicsContextCairo.h"
#include "NicosiaContentLayerTextureMapperImpl.h"
#include <array>

namespace WebCore {

class ImageBufferCairoGLDisplayDelegate;

class ImageBufferCairoGLSurfaceBackend : public ImageBufferCairoSurfaceBackend, public Nicosia::ContentLayerTextureMapperImpl::Client {
    WTF_MAKE_ISO_ALLOCATED(ImageBufferCairoGLSurfaceBackend);
    WTF_MAKE_NONCOPYABLE(ImageBufferCairoGLSurfaceBackend);
public:
    static unsigned calculateBytesPerRow(const IntSize& backendSize);
    static size_t calculateMemoryCost(const Parameters&);

    static std::unique_ptr<ImageBufferCairoGLSurfaceBackend> create(const Parameters&, const ImageBuffer::CreationContext&);

    virtual ~ImageBufferCairoGLSurfaceBackend();

    RefPtr<GraphicsLayerContentsDisplayDelegate> layerContentsDisplayDelegate() const final;
    bool copyToPlatformTexture(GraphicsContextGL&, GCGLenum, PlatformGLObject, GCGLenum, bool, bool) const final;

private:
    ImageBufferCairoGLSurfaceBackend(const Parameters&, const std::array<uint32_t, 2>&, const std::array<RefPtr<cairo_surface_t>, 2>&);

    unsigned bytesPerRow() const override;
    RefPtr<PixelBuffer> getPixelBuffer(const PixelBufferFormat& outputFormat, const IntRect&, const ImageBufferAllocator&) const override;
    void putPixelBuffer(const PixelBuffer&, const IntRect& srcRect, const IntPoint& destPoint, AlphaPremultiplication destFormat) override;
    IntSize backendSize() const override;
    RefPtr<NativeImage> copyNativeImage(BackingStoreCopy) const override;

    void swapBuffersIfNeeded() final;

    RefPtr<Nicosia::ContentLayer> m_nicosiaLayer;
    RefPtr<ImageBufferCairoGLDisplayDelegate> m_layerContentsDisplayDelegate;

    std::array<uint32_t, 2> m_textures;
    std::array<RefPtr<cairo_surface_t>, 2> m_surfaces;

    RefPtr<cairo_t> m_compositorContext;
};

} // namespace WebCore

#endif // ENABLE(ACCELERATED_2D_CANVAS)
